#include "Slic3r/Biz/Format/OBJ.hpp"
#include "Slic3r/Biz/Format/ObjColors.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace Slic3r;
using namespace Slic3r::Biz;
using namespace Slic3r::Biz::Format::ObjColors;

namespace {

std::string test_obj_path(const char* name)
{
    return std::string(TEST_DATA_DIR) + "/test_obj/" + name;
}

// 12 triangles per cube, in file order: the first 12 belong to the first cube.
void check_two_cubes(const LoadedObj& loaded, const Rgba& first_cube, const Rgba& second_cube)
{
    REQUIRE(loaded.mesh.facets_count() == 24);
    REQUIRE(loaded.coloring.has_value());
    const Coloring& coloring = *loaded.coloring;
    REQUIRE(coloring.colors.size() == 2);
    REQUIRE(coloring.face_state.size() == 24);

    // The bigger cube is the most used color: state 1.
    CHECK(coloring.colors[0] == first_cube);
    CHECK(coloring.colors[1] == second_cube);
    for (size_t i = 0; i < 12; ++i)
        CHECK(coloring.face_state[i] == 1);
    for (size_t i = 12; i < 24; ++i)
        CHECK(coloring.face_state[i] == 2);
}

} // namespace

TEST_CASE("parse_mtl reads the diffuse color of every material", "[objcolors]")
{
    std::istringstream in(
        "# comment\n"
        "newmtl Red\r\n"
        "Ka 1 1 1\n"
        "Kd 1.0 0.0 0.0\n"
        "newmtl Mat with spaces \n"
        "  Kd 0.5 0.25 1\n"
        "newmtl NoDiffuse\n"
        "Ks 1 1 1\n"
        "newmtl Broken\n"
        "Kd spectral file.rfl\n"
        "Kd 0.1 0.2\n"
    );
    const auto materials = parse_mtl(in);
    REQUIRE(materials.size() == 2);
    CHECK(materials.at("Red") == Rgba{255, 0, 0, 255});
    CHECK(materials.at("Mat with spaces") == Rgba{128, 64, 255, 255});
    CHECK(materials.count("NoDiffuse") == 0);
    CHECK(materials.count("Broken") == 0);
}

TEST_CASE("OBJ vertex colors become painting states", "[objcolors]")
{
    const auto loaded = load_obj_colored(test_obj_path("color_vertex.obj"));
    REQUIRE(loaded.has_value());
    check_two_cubes(*loaded, Rgba{255, 0, 0, 255}, Rgba{0, 0, 255, 255});
}

TEST_CASE("OBJ materials of the .mtl file become painting states", "[objcolors]")
{
    const auto loaded = load_obj_colored(test_obj_path("color_mtl.obj"));
    REQUIRE(loaded.has_value());
    check_two_cubes(*loaded, Rgba{255, 0, 0, 255}, Rgba{0, 0, 255, 255});
}

TEST_CASE("OBJ without colors has no coloring and loads as before", "[objcolors]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "colorslicer_plain.obj";
    {
        std::ofstream out(path);
        out << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0 1.0\nf 1 2 3 4\n";
    }
    const auto loaded = load_obj_colored(path.string());
    std::filesystem::remove(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->mesh.facets_count() == 2);
    CHECK_FALSE(loaded->coloring.has_value());
}

TEST_CASE("OBJ vertex colors written as 0..255 and a missing .mtl", "[objcolors]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "colorslicer_bytes.obj";
    {
        std::ofstream out(path);
        out << "mtllib does_not_exist.mtl\n"
               "v 0 0 0 255 0 0\nv 1 0 0 255 0 0\nv 1 1 0 255 0 0\n"
               "v 5 0 0 0 128 0\nv 6 0 0 0 128 0\nv 6 1 0 0 128 0\n"
               "v 9 0 0\nv 10 0 0\nv 10 1 0\n"
               "f 1 2 3\nf 4 5 6\nf 7 8 9\n";
    }
    const auto loaded = load_obj_colored(path.string());
    std::filesystem::remove(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->coloring.has_value());
    const Coloring& coloring = *loaded->coloring;
    REQUIRE(coloring.colors.size() == 2);
    REQUIRE(coloring.face_state.size() == 3);
    CHECK(coloring.face_state[0] != 0);
    CHECK(coloring.face_state[1] != 0);
    CHECK(coloring.face_state[0] != coloring.face_state[1]);
    CHECK(coloring.face_state[2] == 0); // vertices without color stay unpainted
    const Rgba green = coloring.colors[coloring.face_state[1] - 1];
    CHECK(green == Rgba{0, 128, 0, 255});
}
