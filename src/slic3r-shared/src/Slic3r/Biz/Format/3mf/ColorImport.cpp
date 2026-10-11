#include "Slic3r/Biz/Format/3mf/ColorImport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

// CIE Lab (D65) of an sRGB color: Euclidean distance there follows what the eye sees much better than RGB.
using Lab = std::array<float, 3>;

Lab to_lab(const Rgba& color)
{
    const auto linear = [](const std::uint8_t channel) {
        const double v = channel / 255.;
        return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    const double r = linear(color.r), g = linear(color.g), b = linear(color.b);
    const double x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047;
    const double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
    const double z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883;
    const auto f = [](const double t) { return t > 216. / 24389. ? std::cbrt(t) : (24389. / 27. * t + 16.) / 116.; };
    const double fx = f(x), fy = f(y), fz = f(z);
    return Lab{float(116. * fy - 16.), float(500. * (fx - fy)), float(200. * (fy - fz))};
}

float lab_distance_sq(const Lab& a, const Lab& b)
{
    const float dl = a[0] - b[0], da = a[1] - b[1], db = a[2] - b[2];
    return dl * dl + da * da + db * db;
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

namespace {

struct QuantizeInput
{
    Rgba color;
    double weight{0.};
};

// Weighted k-means in Lab. Returns the cluster of every input and the colors of the clusters,
// ordered so that every prefix of the list is a good palette (see PaletteBuilder::build()).
PaletteResult quantize(const std::vector<QuantizeInput>& inputs, const std::size_t k_wanted)
{
    const std::size_t n = inputs.size();
    const std::size_t k = std::min(k_wanted, n);

    std::vector<Lab> lab(n);
    // The most used shades must not take all the centers: the clustering sees the square root of the usage.
    std::vector<double> weight(n);
    for (std::size_t i = 0; i < n; ++i) {
        lab[i]    = to_lab(inputs[i].color);
        weight[i] = std::sqrt(std::max(inputs[i].weight, 1e-12));
    }

    // Deterministic k-means++: first the heaviest color, then the one with the most weight * distance^2.
    std::vector<Lab> center;
    center.reserve(k);
    std::size_t first = 0;
    for (std::size_t i = 1; i < n; ++i) {
        if (inputs[i].weight > inputs[first].weight)
            first = i;
    }
    center.push_back(lab[first]);
    std::vector<float> nearest_sq(n);
    for (std::size_t i = 0; i < n; ++i)
        nearest_sq[i] = lab_distance_sq(lab[i], center[0]);
    while (center.size() < k) {
        std::size_t best = 0;
        double best_score = -1.;
        for (std::size_t i = 0; i < n; ++i) {
            const double score = weight[i] * nearest_sq[i];
            if (score > best_score) {
                best_score = score;
                best       = i;
            }
        }
        if (best_score <= 0.)
            break; // fewer different colors than centers
        center.push_back(lab[best]);
        for (std::size_t i = 0; i < n; ++i)
            nearest_sq[i] = std::min(nearest_sq[i], lab_distance_sq(lab[i], center.back()));
    }

    const auto closest = [&center](const Lab& value) {
        std::size_t best = 0;
        float best_sq    = std::numeric_limits<float>::max();
        for (std::size_t c = 0; c < center.size(); ++c) {
            const float d = lab_distance_sq(value, center[c]);
            if (d < best_sq) {
                best_sq = d;
                best    = c;
            }
        }
        return best;
    };

    std::vector<std::size_t> cluster(n, 0);
    for (int iteration = 0; iteration < 16; ++iteration) {
        bool changed = false;
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t c = closest(lab[i]);
            if (c != cluster[i]) {
                cluster[i] = c;
                changed    = true;
            }
        }
        std::vector<std::array<double, 3>> sum(center.size(), {0., 0., 0.});
        std::vector<double> total(center.size(), 0.);
        for (std::size_t i = 0; i < n; ++i) {
            for (int j = 0; j < 3; ++j)
                sum[cluster[i]][j] += weight[i] * lab[i][j];
            total[cluster[i]] += weight[i];
        }
        for (std::size_t c = 0; c < center.size(); ++c) {
            if (total[c] > 0.)
                center[c] = Lab{float(sum[c][0] / total[c]), float(sum[c][1] / total[c]), float(sum[c][2] / total[c])};
        }
        if (!changed)
            break;
    }
    for (std::size_t i = 0; i < n; ++i)
        cluster[i] = closest(lab[i]);

    // Color of a cluster: weighted mean of its members in sRGB; usage of a cluster: sum of the real usage.
    const std::size_t clusters = center.size();
    std::vector<std::array<double, 3>> rgb_sum(clusters, {0., 0., 0.});
    std::vector<double> usage(clusters, 0.);
    for (std::size_t i = 0; i < n; ++i) {
        const double w = std::max(inputs[i].weight, 1e-12);
        rgb_sum[cluster[i]][0] += w * inputs[i].color.r;
        rgb_sum[cluster[i]][1] += w * inputs[i].color.g;
        rgb_sum[cluster[i]][2] += w * inputs[i].color.b;
        usage[cluster[i]] += w;
    }
    std::vector<Rgba> cluster_color(clusters);
    std::vector<Lab> cluster_lab(clusters);
    for (std::size_t c = 0; c < clusters; ++c) {
        if (usage[c] > 0.) {
            const auto channel = [&](const int j) {
                return static_cast<std::uint8_t>(std::clamp(std::lround(rgb_sum[c][j] / usage[c]), 0L, 255L));
            };
            cluster_color[c] = Rgba{channel(0), channel(1), channel(2), 255};
        }
        cluster_lab[c] = to_lab(cluster_color[c]);
    }

    // Order: the most used first, then always the cluster with the biggest sqrt(usage) * distance to those chosen.
    std::vector<std::size_t> order;
    std::vector<bool> taken(clusters, false);
    std::vector<float> distance(clusters, std::numeric_limits<float>::max());
    while (order.size() < clusters) {
        std::size_t best = clusters;
        double best_score = -1.;
        for (std::size_t c = 0; c < clusters; ++c) {
            if (taken[c] || usage[c] <= 0.)
                continue;
            const double score = order.empty() ? usage[c] : std::sqrt(usage[c]) * std::sqrt(double(distance[c]));
            if (score > best_score) {
                best_score = score;
                best       = c;
            }
        }
        if (best == clusters)
            break;
        taken[best] = true;
        order.push_back(best);
        for (std::size_t c = 0; c < clusters; ++c)
            distance[c] = std::min(distance[c], lab_distance_sq(cluster_lab[c], cluster_lab[best]));
    }

    std::vector<unsigned> state_of_cluster(clusters, 0);
    PaletteResult result;
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        result.colors.push_back(cluster_color[order[rank]]);
        state_of_cluster[order[rank]] = static_cast<unsigned>(rank + 1);
    }
    result.state_of_input.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        result.state_of_input[i] = state_of_cluster[cluster[i]];
    return result;
}

} // namespace

PaletteResult PaletteBuilder::build(std::size_t max_colors) const
{
    max_colors = std::clamp<std::size_t>(max_colors, 1, MAX_TRIANGLE_STATE);

    if (m_entries.size() > QUANTIZE_FACTOR * max_colors) {
        std::vector<QuantizeInput> inputs;
        inputs.reserve(m_entries.size());
        for (const Entry& entry : m_entries)
            inputs.push_back(QuantizeInput{entry.color, entry.weight});
        return quantize(inputs, max_colors);
    }

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
