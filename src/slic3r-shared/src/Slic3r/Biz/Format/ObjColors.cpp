#include "Slic3r/Biz/Format/ObjColors.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string_view>
#include <system_error>

#include <fast_float.h>

#include "objparser.hpp"

namespace Slic3r::Biz::Format::ObjColors {

namespace {

constexpr const char* WHITESPACE = " \t\r\n";

std::string trim(const std::string& text)
{
    const std::size_t first = text.find_first_not_of(WHITESPACE);
    if (first == std::string::npos)
        return {};
    const std::size_t last = text.find_last_not_of(WHITESPACE);
    return text.substr(first, last - first + 1);
}

std::vector<std::string> split_words(const std::string& line)
{
    std::vector<std::string> words;
    std::size_t pos = 0;
    while (true) {
        pos = line.find_first_not_of(WHITESPACE, pos);
        if (pos == std::string::npos)
            break;
        const std::size_t end = line.find_first_of(WHITESPACE, pos);
        words.push_back(line.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
        pos = end;
        if (pos == std::string::npos)
            break;
    }
    return words;
}

bool parse_double(const std::string& word, double& out)
{
    const char* begin = word.data();
    const char* end   = begin + word.size();
    const auto [ptr, ec] = fast_float::from_chars(begin, end, out);
    return ec == std::errc() && ptr == end;
}

std::uint8_t to_byte(const double value)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0., 1.) * 255.));
}

double triangle_area(const float* a, const float* b, const float* c)
{
    const double ux = double(b[0]) - a[0], uy = double(b[1]) - a[1], uz = double(b[2]) - a[2];
    const double vx = double(c[0]) - a[0], vy = double(c[1]) - a[1], vz = double(c[2]) - a[2];
    const double cx = uy * vz - uz * vy;
    const double cy = uz * vx - ux * vz;
    const double cz = ux * vy - uy * vx;
    return 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
}

} // namespace

std::unordered_map<std::string, Rgba> parse_mtl(std::istream& stream)
{
    std::unordered_map<std::string, Rgba> materials;
    std::string current;
    std::string line;
    while (std::getline(stream, line)) {
        const std::vector<std::string> words = split_words(line);
        if (words.empty())
            continue;

        if (words[0] == "newmtl") {
            const std::size_t keyword = line.find("newmtl");
            current                   = trim(line.substr(keyword + 6));
        } else if (words[0] == "Kd" && words.size() >= 4 && !current.empty()) {
            double r = 0., g = 0., b = 0.;
            if (parse_double(words[1], r) && parse_double(words[2], g) && parse_double(words[3], b))
                materials[current] = Rgba{to_byte(r), to_byte(g), to_byte(b), 255};
        }
    }
    return materials;
}

std::optional<Coloring> color_faces(
    const ObjParser::ObjData& data, const MtlReader& read_mtl, const std::size_t max_colors
)
{
    std::unordered_map<std::string, Rgba> materials;
    if (read_mtl) {
        for (const std::string& library : data.mtllibs) {
            const std::optional<std::string> text = read_mtl(trim(library));
            if (!text.has_value())
                continue;
            std::istringstream in(*text);
            for (const auto& [name, color] : parse_mtl(in))
                materials[name] = color;
        }
    }

    const std::size_t coordinate_count = data.coordinates.size() / 4;
    const bool has_vertex_colors       = !data.vertexColors.empty() && data.vertexColors.size() >= coordinate_count * 3;
    if (!has_vertex_colors && materials.empty())
        return std::nullopt;

    // Colors are normally 0..1, a few exporters write 0..255.
    double vertex_color_scale = 1.;
    if (has_vertex_colors) {
        const float max_value = *std::max_element(data.vertexColors.begin(), data.vertexColors.end());
        if (max_value > 1.f)
            vertex_color_scale = 1. / 255.;
    }

    ColorImport::PaletteBuilder palette;
    std::vector<std::optional<std::size_t>> color_id_of_face;

    std::size_t next_usemtl = 0;
    std::optional<Rgba> material_color;
    std::vector<int> corners;

    for (std::size_t i = 0; i < data.vertices.size();) {
        if (data.vertices[i].coordIdx == -1) {
            ++i;
            continue;
        }

        while (next_usemtl < data.usemtls.size() && static_cast<std::size_t>(data.usemtls[next_usemtl].vertexIdxFirst) <= i) {
            const auto found = materials.find(trim(data.usemtls[next_usemtl].name));
            material_color   = found == materials.end() ? std::nullopt : std::optional<Rgba>(found->second);
            ++next_usemtl;
        }

        corners.clear();
        for (; i < data.vertices.size() && data.vertices[i].coordIdx != -1; ++i)
            corners.push_back(data.vertices[i].coordIdx);
        if (corners.size() < 3)
            continue;

        const bool valid = std::all_of(corners.begin(), corners.end(), [coordinate_count](const int index) {
            return index >= 0 && static_cast<std::size_t>(index) < coordinate_count;
        });

        std::optional<Rgba> color;
        if (valid && has_vertex_colors) {
            double sum[3] = {0., 0., 0.};
            bool complete = true;
            for (const int index : corners) {
                const float* c = &data.vertexColors[static_cast<std::size_t>(index) * 3];
                if (c[0] < 0.f || c[1] < 0.f || c[2] < 0.f) {
                    complete = false;
                    break;
                }
                for (int k = 0; k < 3; ++k)
                    sum[k] += double(c[k]) * vertex_color_scale;
            }
            if (complete) {
                const double n = static_cast<double>(corners.size());
                color          = Rgba{to_byte(sum[0] / n), to_byte(sum[1] / n), to_byte(sum[2] / n), 255};
            }
        }
        if (!color)
            color = material_color;

        if (!color) {
            color_id_of_face.push_back(std::nullopt);
            continue;
        }

        double area = 0.;
        if (valid) {
            const auto point = [&data](const int index) { return &data.coordinates[static_cast<std::size_t>(index) * 4]; };
            area += triangle_area(point(corners[0]), point(corners[1]), point(corners[2]));
            if (corners.size() == 4)
                area += triangle_area(point(corners[0]), point(corners[2]), point(corners[3]));
        }
        color_id_of_face.push_back(palette.add(*color, area + 1e-9));
    }

    if (palette.size() == 0)
        return std::nullopt;

    const ColorImport::PaletteResult reduced = palette.build(max_colors);

    Coloring result;
    result.colors = reduced.colors;
    result.face_state.reserve(color_id_of_face.size());
    for (const std::optional<std::size_t>& id : color_id_of_face)
        result.face_state.push_back(id.has_value() ? reduced.state_of_input[*id] : 0u);
    return result;
}

} // namespace Slic3r::Biz::Format::ObjColors
