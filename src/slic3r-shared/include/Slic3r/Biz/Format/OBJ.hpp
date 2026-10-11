#pragma once

#include "Slic3r/Domain/TriangleMesh.hpp"
#include "tl/expected.hpp"
#include "Slic3r/Biz/Format/ObjColors.hpp"

#include <optional>

namespace Slic3r::Domain {
    class Model;
}

namespace Slic3r::Biz {

tl::expected<Domain::TriangleMesh, std::string> load_obj(const std::string& path);

struct LoadedObj
{
    Domain::TriangleMesh mesh;
    /// Colors of the file (vertex colors or materials). face_state has one state per triangle of mesh.
    /// Empty when the file has no colors.
    std::optional<Format::ObjColors::Coloring> coloring;
};

/**
 * Like load_obj(), and reads the colors of the file: vertex colors (`v x y z r g b`) and the
 * `Kd` of the materials of the `.mtl` files next to the OBJ.
 */
tl::expected<LoadedObj, std::string> load_obj_colored(const std::string& path);
bool store_obj(const std::string& path, const Domain::TriangleMesh& mesh);
bool store_obj(const std::string& path, Domain::Model* model);

}; // namespace Slic3r::Biz
