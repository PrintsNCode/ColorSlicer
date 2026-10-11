#pragma once

#include <cstddef>
#include <functional>
#include <istream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Slic3r/Biz/Format/3mf/ColorImport.hpp"

namespace ObjParser {
struct ObjData;
}

/**
 * Colors of OBJ files: vertex colors (`v x y z r g b`, as written by Blender, MeshLab, ...) and
 * materials (`usemtl` + the `Kd` of the `.mtl` file named by `mtllib`).
 *
 * Plain C++ without dependencies on the rest of the application, so it can be unit tested alone.
 * Textures (`map_Kd`) are not supported.
 */
namespace Slic3r::Biz::Format::ObjColors {

using ColorImport::Rgba;

/** Reads the diffuse color (Kd) of every `newmtl` of an MTL file. Materials without Kd are left out. */
std::unordered_map<std::string, Rgba> parse_mtl(std::istream& stream);

/** Gives the content of the `.mtl` file named by a `mtllib` line, or nullopt when it can't be read. */
using MtlReader = std::function<std::optional<std::string>(const std::string& mtllib_name)>;

struct Coloring
{
    /// Colors ordered by usage, most used first. State / extruder number of a color is index + 1.
    std::vector<Rgba> colors;
    /// State of every face (see color_faces()) or, once expanded by the loader, of every triangle.
    /// 0 means the face has no color.
    std::vector<unsigned> face_state;
};

/// More colors than this are merged into the nearest ones.
inline constexpr std::size_t MAX_FILE_COLORS = 16;

/**
 * Colors every face of the OBJ.
 *
 * A face takes the average of its vertex colors when all its corners have one, otherwise the Kd
 * of its material. Colors are reduced to at most max_colors, weighted by the area they cover.
 *
 * @return One state per face (faces in file order, a quad is one face), or nullopt when the file
 *         has neither vertex colors nor usable materials.
 */
std::optional<Coloring> color_faces(
    const ObjParser::ObjData& data, const MtlReader& read_mtl, std::size_t max_colors = MAX_FILE_COLORS
);

} // namespace Slic3r::Biz::Format::ObjColors
