#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/**
 * Helpers used when a 3MF written by another application (Blender, Bambu Studio, OrcaSlicer,
 * CAD tools, ...) carries colors and PrusaSlicer has to map them onto its extruders.
 *
 * Everything in here is plain C++ without any dependency on the rest of the application, so it
 * can be unit tested in isolation.
 */
namespace Slic3r::Biz::Format::ColorImport {

struct Rgba
{
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};
    std::uint8_t a{255};

    bool operator==(const Rgba&) const = default;
};

/**
 * Parses "#RRGGBB", "#RRGGBBAA", "RRGGBB" or "RRGGBBAA" (case-insensitive).
 * Alpha defaults to 255. Returns nullopt for anything else.
 */
std::optional<Rgba> parse_color(std::string_view text);

/** Formats as "#RRGGBB" (upper case). Alpha is dropped: it has no meaning for printing. */
std::string to_hex(const Rgba& color);

/**
 * Serializes the state of a whole (not split) triangle in the format understood by
 * FacetsAnnotation::set_triangle_from_string(), which is also what BambuStudio and OrcaSlicer
 * write into the `paint_color` attribute.
 *
 * State 0 (not painted) gives an empty string, 1 and 2 give "4" and "8", 3..16 give "0C".."DC",
 * 17..254 use the extended form.
 *
 * @param state Triangle state (extruder number, 1-based).
 */
std::string encode_leaf_state(unsigned state);

/** Largest state encode_leaf_state() can represent. */
inline constexpr unsigned MAX_TRIANGLE_STATE = 254;

/** Squared perceptual-ish distance ("redmean" approximation) between two colors. */
double distance_sq(const Rgba& a, const Rgba& b);

/** Index of the candidate closest to color. candidates must not be empty. */
std::size_t nearest_index(const Rgba& color, const std::vector<Rgba>& candidates);

struct PaletteResult
{
    /// Colors ordered by usage, most used first. Triangle state / extruder number is index + 1.
    std::vector<Rgba> colors;
    /// For every color id given out by PaletteBuilder::add(): its 1-based state.
    /// Colors that did not make it into the palette point to their nearest palette color.
    std::vector<unsigned> state_of_input;
};

/**
 * Collects the colors found in a file together with how much surface uses each of them, then
 * reduces them to at most as many colors as the printer has extruders.
 */
class PaletteBuilder
{
public:
    /**
     * Registers a color. Colors with equal RGB share one id (alpha is ignored).
     *
     * @param weight How much this use of the color counts (e.g. triangle area).
     * @return Id of the color, valid as index into PaletteResult::state_of_input.
     */
    std::size_t add(const Rgba& color, double weight = 1.0);

    std::size_t size() const { return m_entries.size(); }
    const Rgba& color(std::size_t id) const { return m_entries[id].color; }

    /**
     * Reduces the colors to at most max_colors. max_colors is clamped to [1, MAX_TRIANGLE_STATE].
     *
     * - Up to QUANTIZE_FACTOR * max_colors different colors (flat colors of a CAD or Blender model):
     *   keeps the max_colors most used colors, merging every other one into its nearest kept color.
     *   Colors ordered by usage, most used first.
     * - More than that (continuous colors of a scan, a photogrammetry or an AI generated model): a
     *   weighted k-means in the CIE Lab color space finds max_colors representative colors, so that
     *   a few small but very different regions are not lost behind the most used shades. They are
     *   ordered so that every prefix of the list is a good palette: the most used color first, then
     *   each time the color that is both used and far from the ones already chosen.
     */
    PaletteResult build(std::size_t max_colors) const;

    /// build() quantizes when there are more than QUANTIZE_FACTOR * max_colors different colors.
    static constexpr std::size_t QUANTIZE_FACTOR = 2;

private:
    struct Entry
    {
        Rgba color;
        double weight{0.};
    };

    std::vector<Entry> m_entries;
    // RGB packed as 0xRRGGBB -> index into m_entries.
    std::unordered_map<std::uint32_t, std::size_t> m_index_by_rgb;
};

/**
 * For every color of source returns the 1-based index of the nearest color of target.
 * Returns zeros when target is empty.
 */
std::vector<unsigned> map_to_nearest(const std::vector<Rgba>& source, const std::vector<Rgba>& target);

struct ExtruderPlan
{
    /// Color of extruder j + 1. At most slot_count items.
    std::vector<Rgba> extruder_colors;
    /// For every color of the file: the extruder (1-based) that prints it.
    /// All zeros when the file uses no color at all.
    std::vector<unsigned> extruder_of_file_color;
};

/**
 * Decides which extruder prints each color of a file.
 *
 * The first slot_count colors that the geometry really uses (in the order of the file) get an
 * extruder of their own. Every other color is printed with the extruder of the nearest one.
 *
 * @param file_colors Colors of the file. Color i is state i + 1 of the painting in the loaded model.
 * @param used        Which of the colors the geometry uses. Colors past its end count as used.
 * @param slot_count  How many extruders the printer has.
 */
ExtruderPlan plan_extruders(
    const std::vector<Rgba>& file_colors, const std::vector<bool>& used, std::size_t slot_count
);

} // namespace Slic3r::Biz::Format::ColorImport
