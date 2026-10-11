#include "Slic3r/Biz/Format/3mf/ColorImport.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <numeric>

namespace Slic3r::Biz::Format::ColorImport {

namespace {

int hex_value(const char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F')
        return 10 + (ch - 'A');
    return -1;
}

char hex_digit(const unsigned nibble)
{
    return static_cast<char>(nibble < 10 ? '0' + nibble : 'A' + (nibble - 10));
}

std::uint32_t pack_rgb(const Rgba& color)
{
    return (std::uint32_t(color.r) << 16) | (std::uint32_t(color.g) << 8) | std::uint32_t(color.b);
}

} // namespace

std::optional<Rgba> parse_color(std::string_view text)
{
    if (!text.empty() && text.front() == '#')
        text.remove_prefix(1);

    if (text.size() != 6 && text.size() != 8)
        return std::nullopt;

    std::uint8_t channels[4] = {0, 0, 0, 255};
    for (std::size_t channel = 0; channel * 2 < text.size(); ++channel) {
        const int hi = hex_value(text[channel * 2]);
        const int lo = hex_value(text[channel * 2 + 1]);
        if (hi < 0 || lo < 0)
            return std::nullopt;
        channels[channel] = static_cast<std::uint8_t>(hi * 16 + lo);
    }

    return Rgba{channels[0], channels[1], channels[2], channels[3]};
}

std::string to_hex(const Rgba& color)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", color.r, color.g, color.b);
    return buffer;
}

std::string encode_leaf_state(const unsigned state)
{
    if (state == 0)
        return {};

    // Nibbles are read from the end of the string towards its beginning, so the first nibble
    // the decoder sees (the "code") is the last character.
    if (state <= 2) {
        return std::string(1, hex_digit(state << 2));
    }

    if (state <= 16) {
        std::string result;
        result += hex_digit(state - 3);
        result += 'C';
        return result;
    }

    const unsigned extended = std::min(state, MAX_TRIANGLE_STATE) - 17;
    std::string result;
    result += hex_digit(extended >> 4);
    result += hex_digit(extended & 0xF);
    result += 'E';
    result += 'C';
    return result;
}

double distance_sq(const Rgba& a, const Rgba& b)
{
    const double red_mean = (double(a.r) + double(b.r)) / 2.;
    const double dr       = double(a.r) - double(b.r);
    const double dg       = double(a.g) - double(b.g);
    const double db       = double(a.b) - double(b.b);
    return (2. + red_mean / 256.) * dr * dr + 4. * dg * dg + (2. + (255. - red_mean) / 256.) * db * db;
}

std::size_t nearest_index(const Rgba& color, const std::vector<Rgba>& candidates)
{
    std::size_t best          = 0;
    double best_distance      = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const double distance = distance_sq(color, candidates[i]);
        if (distance < best_distance) {
            best_distance = distance;
            best          = i;
        }
    }
    return best;
}

std::size_t PaletteBuilder::add(const Rgba& color, const double weight)
{
    const auto [it, inserted] = m_index_by_rgb.try_emplace(pack_rgb(color), m_entries.size());
    if (inserted)
        m_entries.push_back(Entry{color, 0.});

    m_entries[it->second].weight += weight;
    return it->second;
}

PaletteResult PaletteBuilder::build(std::size_t max_colors) const
{
    max_colors = std::clamp<std::size_t>(max_colors, 1, MAX_TRIANGLE_STATE);

    // Most used first. Stable, so colors that tie keep the order in which the file introduced them.
    std::vector<std::size_t> order(m_entries.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(
        order.begin(),
        order.end(),
        [this](const std::size_t lhs, const std::size_t rhs)
        { return m_entries[lhs].weight > m_entries[rhs].weight; }
    );

    const std::size_t kept = std::min(max_colors, order.size());

    PaletteResult result;
    result.colors.reserve(kept);
    result.state_of_input.assign(m_entries.size(), 0);

    for (std::size_t rank = 0; rank < kept; ++rank) {
        result.colors.push_back(m_entries[order[rank]].color);
        result.state_of_input[order[rank]] = static_cast<unsigned>(rank + 1);
    }

    for (std::size_t rank = kept; rank < order.size(); ++rank) {
        const std::size_t id = order[rank];
        result.state_of_input[id] =
            static_cast<unsigned>(nearest_index(m_entries[id].color, result.colors) + 1);
    }

    return result;
}

std::vector<unsigned> map_to_nearest(const std::vector<Rgba>& source, const std::vector<Rgba>& target)
{
    std::vector<unsigned> result(source.size(), 0);
    if (target.empty())
        return result;

    for (std::size_t i = 0; i < source.size(); ++i)
        result[i] = static_cast<unsigned>(nearest_index(source[i], target) + 1);

    return result;
}

ExtruderPlan plan_extruders(
    const std::vector<Rgba>& file_colors, const std::vector<bool>& used, const std::size_t slot_count
)
{
    ExtruderPlan plan;
    plan.extruder_of_file_color.assign(file_colors.size(), 0);

    std::vector<std::size_t> kept; // indices into file_colors
    for (std::size_t i = 0; i < file_colors.size() && kept.size() < slot_count; ++i) {
        if (i >= used.size() || used[i])
            kept.push_back(i);
    }

    if (kept.empty())
        return plan;

    plan.extruder_colors.reserve(kept.size());
    for (const std::size_t index : kept) {
        plan.extruder_colors.push_back(file_colors[index]);
        plan.extruder_of_file_color[index] = static_cast<unsigned>(plan.extruder_colors.size());
    }

    for (std::size_t i = 0; i < file_colors.size(); ++i) {
        if (plan.extruder_of_file_color[i] == 0)
            plan.extruder_of_file_color[i] =
                static_cast<unsigned>(nearest_index(file_colors[i], plan.extruder_colors) + 1);
    }

    return plan;
}

} // namespace Slic3r::Biz::Format::ColorImport
