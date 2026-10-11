#include "Slic3r/Biz/Format/3mf/ForeignPrint.hpp"

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <type_traits>

namespace Slic3r::Biz::Format::ForeignPrint {

namespace {

// ----------------------------------------------------------------------------
// Parsing
// ----------------------------------------------------------------------------

std::string json_scalar_to_string(const nlohmann::json& value)
{
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "1" : "0";
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_float()) {
        std::string s = std::to_string(value.get<double>());
        s.erase(s.find_last_not_of('0') + 1);
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
        return s;
    }
    return {};
}

// ----------------------------------------------------------------------------
// Translation
// ----------------------------------------------------------------------------

using Foreign = std::map<std::string, std::string>;

const std::string* find(const Foreign& foreign, const char* key)
{
    const auto it = foreign.find(key);
    if (it == foreign.end() || it->second.empty() || it->second == "nil") {
        return nullptr;
    }
    return &it->second;
}

bool is_true(const std::string& value)
{
    return value == "1" || value == "true";
}

struct Mapper
{
    const Foreign& foreign;
    Settings& out;

    void emit(const char* key, std::string value) const
    {
        out.emplace_back(key, std::move(value));
    }

    // Same value, different key.
    void direct(const char* from, const char* to) const
    {
        if (const std::string* v = find(foreign, from)) {
            emit(to, *v);
        }
    }

    // Value goes through a translation table. Values not in the table are dropped.
    template <size_t N>
    void mapped(const char* from, const char* to, const std::pair<const char*, const char*> (&table)[N]) const
    {
        const std::string* v = find(foreign, from);
        if (v == nullptr) {
            return;
        }
        for (const auto& [src, dst] : table) {
            if (*v == src) {
                emit(to, dst);
                return;
            }
        }
    }

    void boolean(const char* from, const char* to) const
    {
        if (const std::string* v = find(foreign, from)) {
            emit(to, is_true(*v) ? "1" : "0");
        }
    }
};

void translate_supports(const Mapper& m, const Foreign& f)
{
    const std::string* enable = find(f, "enable_support");
    if (enable == nullptr) {
        return;
    }
    if (!is_true(*enable)) {
        m.emit("support_material", "none");
        return;
    }

    const std::string* type = find(f, "support_type");
    const std::string type_str = type ? *type : "normal(auto)";
    const bool manual = type_str.find("(manual)") != std::string::npos;
    const bool tree = type_str.rfind("tree", 0) == 0;

    m.emit("support_material", manual ? "enforcers_only" : "everywhere");

    if (tree) {
        m.emit("support_material_style", "organic");
    } else {
        const std::string* style = find(f, "support_style");
        m.emit("support_material_style", style && *style == "snug" ? "snug" : "grid");
    }
}

void translate_ironing(const Mapper& m, const Foreign& f)
{
    const std::string* v = find(f, "ironing_type");
    if (v == nullptr) {
        return;
    }
    if (*v == "no ironing") {
        m.emit("ironing", "0");
        return;
    }
    if (*v == "top" || *v == "topmost" || *v == "solid") {
        m.emit("ironing", "1");
        m.emit("ironing_type", *v);
    }
}

void translate_brim(const Mapper& m, const Foreign& f)
{
    const std::string* v = find(f, "brim_type");
    if (v == nullptr) {
        return;
    }
    // Orca's "auto" brim has no counterpart: the closest is an outer brim.
    if (*v == "auto_brim" || *v == "brim_ears") {
        m.emit("brim_type", "outer_only");
    } else if (*v == "no_brim" || *v == "outer_only" || *v == "inner_only" || *v == "outer_and_inner") {
        m.emit("brim_type", *v);
    }
    m.direct("brim_width", "brim_width");
}

void translate_wall_sequence(const Mapper& m, const Foreign& f)
{
    const std::string* v = find(f, "wall_sequence");
    if (v == nullptr) {
        return;
    }
    if (*v == "outer wall/inner wall") {
        m.emit("external_perimeters_first", "1");
    } else if (*v == "inner wall/outer wall") {
        m.emit("external_perimeters_first", "0");
    }
}

constexpr std::pair<const char*, const char*> SEAM_TABLE[] = {
    {"nearest", "nearest"},
    {"aligned", "aligned"},
    {"aligned_back", "aligned"},
    {"back", "rear"},
    {"random", "random"},
};

constexpr std::pair<const char*, const char*> INFILL_PATTERN_TABLE[] = {
    {"zig-zag", "rectilinear"},
    {"rectilinear", "rectilinear"},
    {"alignedrectilinear", "alignedrectilinear"},
    {"grid", "grid"},
    {"line", "line"},
    {"triangles", "triangles"},
    {"tri-hexagon", "stars"},
    {"cubic", "cubic"},
    {"adaptivecubic", "adaptivecubic"},
    {"supportcubic", "supportcubic"},
    {"concentric", "concentric"},
    {"honeycomb", "honeycomb"},
    {"3dhoneycomb", "3dhoneycomb"},
    {"gyroid", "gyroid"},
    {"hilbertcurve", "hilbertcurve"},
    {"archimedeanchords", "archimedeanchords"},
    {"octagramspiral", "octagramspiral"},
    {"lightning", "lightning"},
};

constexpr std::pair<const char*, const char*> SURFACE_PATTERN_TABLE[] = {
    {"zig-zag", "rectilinear"},
    {"rectilinear", "rectilinear"},
    {"monotonic", "monotonic"},
    {"monotonicline", "monotoniclines"},
    {"alignedrectilinear", "alignedrectilinear"},
    {"concentric", "concentric"},
    {"hilbertcurve", "hilbertcurve"},
    {"archimedeanchords", "archimedeanchords"},
    {"octagramspiral", "octagramspiral"},
};

constexpr std::pair<const char*, const char*> WALL_GENERATOR_TABLE[] = {
    {"classic", "classic"},
    {"arachne", "arachne"},
};

constexpr std::pair<const char*, const char*> FUZZY_TABLE[] = {
    {"none", "none"},
    {"external", "external"},
    {"all", "all"},
    {"allwalls", "all"},
};

constexpr std::pair<const char*, const char*> SHELL_THICKNESS_TABLE[] = {
    {"none", "disabled"},
    {"ensure_critical_only", "partial"},
    {"ensure_moderate", "partial"},
    {"ensure_all", "enabled"},
};

} // namespace

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

std::map<std::string, std::string> parse_project_settings_json(const std::string& json_text)
{
    std::map<std::string, std::string> result;

    const nlohmann::json root = nlohmann::json::parse(json_text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return result;
    }
    for (const auto& [key, value] : root.items()) {
        if (value.is_array() || value.is_object() || value.is_null()) {
            continue;
        }
        result.emplace(key, json_scalar_to_string(value));
    }
    return result;
}

Settings translate(const std::map<std::string, std::string>& foreign)
{
    Settings out;
    const Mapper m{foreign, out};

    // Layers and shells
    m.direct("layer_height", "layer_height");
    m.direct("initial_layer_print_height", "first_layer_height");
    m.direct("wall_loops", "perimeters");
    m.direct("top_shell_layers", "top_solid_layers");
    m.direct("bottom_shell_layers", "bottom_solid_layers");
    m.direct("top_shell_thickness", "top_solid_min_thickness");
    m.direct("bottom_shell_thickness", "bottom_solid_min_thickness");
    m.mapped("wall_generator", "perimeter_generator", WALL_GENERATOR_TABLE);
    translate_wall_sequence(m, foreign);
    m.boolean("detect_thin_wall", "thin_walls");
    m.mapped("ensure_vertical_shell_thickness", "ensure_vertical_shell_thickness", SHELL_THICKNESS_TABLE);
    m.boolean("spiral_mode", "spiral_vase");

    // Seam
    m.mapped("seam_position", "seam_position", SEAM_TABLE);

    // Infill and surfaces
    m.direct("sparse_infill_density", "fill_density");
    m.mapped("sparse_infill_pattern", "fill_pattern", INFILL_PATTERN_TABLE);
    m.mapped("top_surface_pattern", "top_fill_pattern", SURFACE_PATTERN_TABLE);
    m.mapped("bottom_surface_pattern", "bottom_fill_pattern", SURFACE_PATTERN_TABLE);
    m.direct("infill_direction", "fill_angle");
    translate_ironing(m, foreign);

    // Supports
    translate_supports(m, foreign);
    m.direct("support_threshold_angle", "support_material_threshold");
    m.direct("support_top_z_distance", "support_material_contact_distance");
    m.direct("support_bottom_z_distance", "support_material_bottom_contact_distance");
    m.direct("support_interface_top_layers", "support_material_interface_layers");
    m.direct("support_base_pattern_spacing", "support_material_spacing");
    m.direct("support_object_xy_distance", "support_material_xy_spacing");
    m.boolean("support_on_build_plate_only", "support_material_buildplate_only");
    m.direct("raft_layers", "raft_layers");

    // Skirt, brim, prime tower
    translate_brim(m, foreign);
    m.direct("skirt_loops", "skirts");
    m.direct("skirt_distance", "skirt_distance");
    m.direct("skirt_height", "skirt_height");
    m.boolean("enable_prime_tower", "wipe_tower");
    m.direct("prime_tower_width", "wipe_tower_width");

    // Speeds
    m.direct("outer_wall_speed", "external_perimeter_speed");
    m.direct("inner_wall_speed", "perimeter_speed");
    m.direct("small_perimeter_speed", "small_perimeter_speed");
    m.direct("sparse_infill_speed", "infill_speed");
    m.direct("internal_solid_infill_speed", "solid_infill_speed");
    m.direct("top_surface_speed", "top_solid_infill_speed");
    m.direct("bridge_speed", "bridge_speed");
    m.direct("gap_infill_speed", "gap_fill_speed");
    m.direct("initial_layer_speed", "first_layer_speed");
    m.direct("travel_speed", "travel_speed");
    m.direct("support_speed", "support_material_speed");

    // Accelerations
    m.direct("default_acceleration", "default_acceleration");
    m.direct("outer_wall_acceleration", "external_perimeter_acceleration");
    m.direct("inner_wall_acceleration", "perimeter_acceleration");
    m.direct("sparse_infill_acceleration", "infill_acceleration");
    m.direct("top_surface_acceleration", "top_solid_infill_acceleration");
    m.direct("initial_layer_acceleration", "first_layer_acceleration");
    m.direct("bridge_acceleration", "bridge_acceleration");
    m.direct("travel_acceleration", "travel_acceleration");

    // Line widths (0 = automatic in both slicers)
    m.direct("line_width", "extrusion_width");
    m.direct("outer_wall_line_width", "external_perimeter_extrusion_width");
    m.direct("inner_wall_line_width", "perimeter_extrusion_width");
    m.direct("sparse_infill_line_width", "infill_extrusion_width");
    m.direct("internal_solid_infill_line_width", "solid_infill_extrusion_width");
    m.direct("top_surface_line_width", "top_infill_extrusion_width");
    m.direct("initial_layer_line_width", "first_layer_extrusion_width");
    m.direct("support_line_width", "support_material_extrusion_width");

    // Precision
    m.direct("resolution", "resolution");
    m.direct("xy_contour_compensation", "xy_size_compensation");
    m.direct("bridge_flow", "bridge_flow_ratio");
    m.mapped("fuzzy_skin", "fuzzy_skin", FUZZY_TABLE);
    m.direct("fuzzy_skin_thickness", "fuzzy_skin_thickness");

    return out;
}

Settings read_from_archive(mz_zip_archive& archive)
{
    const int index = mz_zip_reader_locate_file(&archive, "Metadata/project_settings.config", nullptr, 0);
    if (index < 0) {
        return {};
    }
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&archive, static_cast<mz_uint>(index), &stat) || stat.m_uncomp_size == 0) {
        return {};
    }
    std::string text(static_cast<size_t>(stat.m_uncomp_size), '\0');
    if (!mz_zip_reader_extract_to_mem(&archive, static_cast<mz_uint>(index), text.data(), text.size(), 0)) {
        return {};
    }
    return translate(parse_project_settings_json(text));
}

} // namespace Slic3r::Biz::Format::ForeignPrint
