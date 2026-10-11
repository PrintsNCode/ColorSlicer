#include "Slic3r/Biz/Format/3mf/ColorImport.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/Domain/Model.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace Slic3r;
using namespace Slic3r::Biz::Format::ColorImport;
using Slic3r::Domain::TriangleSelector::TriangleStateType;

TEST_CASE("encode_leaf_state matches what the painting reader understands", "[colorimport]")
{
    CHECK(encode_leaf_state(0).empty());
    // values written by BambuStudio / OrcaSlicer
    CHECK(encode_leaf_state(1) == "4");
    CHECK(encode_leaf_state(2) == "8");
    CHECK(encode_leaf_state(3) == "0C");
    CHECK(encode_leaf_state(4) == "1C");
    CHECK(encode_leaf_state(16) == "DC");

    // Round trip through the real reader: every state decodes to exactly itself.
    for (unsigned state = 1; state <= MAX_TRIANGLE_STATE; ++state) {
        // Fresh volume every time: used_states accumulate across iterations.
        Domain::Model model = Test::generate_cubes(1, 1);
        Domain::ModelVolume& volume = *model.objects.front()->volumes.front();
        Domain::FacetsAnnotation& facets = volume.mm_segmentation_facets;
        facets.reserve(1);
        facets.set_triangle_from_string(0, encode_leaf_state(state));
        facets.shrink_to_fit();

        const std::vector<bool>& used = facets.get_data().used_states;
        INFO("state " << state);
        CHECK(used[state]);
        for (size_t other = 1; other < used.size(); ++other)
            if (other != state)
                CHECK_FALSE(used[other]);
    }

    // Out of range is clamped, never garbage.
    CHECK(encode_leaf_state(1000) == encode_leaf_state(MAX_TRIANGLE_STATE));
}

TEST_CASE("parse_color and to_hex", "[colorimport]")
{
    auto color = parse_color("#FF8000");
    REQUIRE(color);
    CHECK(*color == Rgba{255, 128, 0, 255});

    color = parse_color("ff8000");
    REQUIRE(color);
    CHECK(*color == Rgba{255, 128, 0, 255});

    color = parse_color("#11223344");
    REQUIRE(color);
    CHECK(*color == Rgba{0x11, 0x22, 0x33, 0x44});

    CHECK_FALSE(parse_color(""));
    CHECK_FALSE(parse_color("#"));
    CHECK_FALSE(parse_color("#FFF"));
    CHECK_FALSE(parse_color("#GG0000"));
    CHECK_FALSE(parse_color("#FF00000"));

    CHECK(to_hex(Rgba{255, 128, 0, 10}) == "#FF8000");
    CHECK(to_hex(*parse_color("#0a0B0c")) == "#0A0B0C");
}

TEST_CASE("nearest_index picks the closest color", "[colorimport]")
{
    const std::vector<Rgba> candidates{{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
    CHECK(nearest_index(Rgba{250, 10, 10}, candidates) == 0);
    CHECK(nearest_index(Rgba{10, 240, 20}, candidates) == 1);
    CHECK(nearest_index(Rgba{0, 20, 200}, candidates) == 2);
    CHECK(nearest_index(Rgba{240, 240, 250}, candidates) == 3);
    CHECK(distance_sq(Rgba{1, 2, 3}, Rgba{1, 2, 3}) == 0.);
}

TEST_CASE("PaletteBuilder keeps the most used colors", "[colorimport]")
{
    PaletteBuilder builder;
    const auto red   = builder.add(Rgba{255, 0, 0}, 1.);
    const auto green = builder.add(Rgba{0, 255, 0}, 5.);
    const auto red2  = builder.add(Rgba{255, 0, 0, 40}, 2.); // same RGB, other alpha
    const auto blue  = builder.add(Rgba{0, 0, 255}, 3.);
    CHECK(red == red2);
    CHECK(builder.size() == 3);

    const PaletteResult all = builder.build(4);
    REQUIRE(all.colors.size() == 3);
    CHECK(all.state_of_input[green] == 1);
    CHECK(all.state_of_input[red] == 2); // ties keep the order of the file
    CHECK(all.state_of_input[blue] == 3);

    const PaletteResult one = builder.build(1);
    REQUIRE(one.colors.size() == 1);
    CHECK(one.state_of_input[green] == 1);
    CHECK(one.state_of_input[red] == 1);
    CHECK(one.state_of_input[blue] == 1);

    CHECK(PaletteBuilder{}.build(4).colors.empty());
}

TEST_CASE("PaletteBuilder merges into a sensible neighbour", "[colorimport]")
{
    PaletteBuilder builder;
    const auto red    = builder.add(Rgba{255, 0, 0}, 10.);
    const auto blue   = builder.add(Rgba{0, 0, 255}, 9.);
    const auto orange = builder.add(Rgba{255, 140, 0}, 1.);
    const auto cyan   = builder.add(Rgba{0, 200, 255}, 1.);

    const PaletteResult result = builder.build(2);
    CHECK(result.state_of_input[red] == 1);
    CHECK(result.state_of_input[blue] == 2);
    CHECK(result.state_of_input[orange] == 1);
    CHECK(result.state_of_input[cyan] == 2);
}

TEST_CASE("PaletteBuilder quantizes continuous colors", "[colorimport]")
{
    // A scan: a hundred dark shades cover almost everything, a few small regions have other colors.
    PaletteBuilder builder;
    for (int i = 0; i < 100; ++i)
        builder.add(Rgba{static_cast<std::uint8_t>(10 + i % 10), static_cast<std::uint8_t>(10 + i / 10), 12}, 100.);
    const auto red    = builder.add(Rgba{220, 30, 30}, 1.);
    const auto orange = builder.add(Rgba{230, 140, 40}, 1.);
    const auto white  = builder.add(Rgba{240, 240, 240}, 1.);

    const PaletteResult result = builder.build(4);
    REQUIRE(result.colors.size() == 4);
    // The dark shades share one color, the small regions are not lost behind them.
    CHECK(result.state_of_input[0] == 1);
    CHECK(result.colors[0].r < 40);
    CHECK(result.state_of_input[red] != result.state_of_input[orange]);
    CHECK(result.state_of_input[red] != result.state_of_input[white]);
    CHECK(result.state_of_input[orange] != result.state_of_input[white]);
    CHECK(result.state_of_input[red] != 1);

    // A gradient with many shades still gives max_colors colors.
    PaletteBuilder gradient;
    for (int i = 0; i < 100; ++i)
        gradient.add(Rgba{static_cast<std::uint8_t>(i * 2), 0, 0}, 1.);
    CHECK(gradient.build(16).colors.size() == 16);
}

TEST_CASE("map_to_nearest", "[colorimport]")
{
    const std::vector<Rgba> source{{250, 0, 0}, {0, 0, 250}};
    const std::vector<Rgba> target{{0, 0, 255}, {255, 0, 0}};
    CHECK(map_to_nearest(source, target) == std::vector<unsigned>{2, 1});
    CHECK(map_to_nearest(source, {}) == std::vector<unsigned>{0, 0});
}

TEST_CASE("plan_extruders", "[colorimport]")
{
    const std::vector<Rgba> file{{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}, {255, 128, 0}};

    SECTION("everything fits: file order")
    {
        const ExtruderPlan plan = plan_extruders(file, {true, true, true, false, false}, 4);
        REQUIRE(plan.extruder_colors.size() == 3);
        CHECK(plan.extruder_colors[0] == file[0]);
        CHECK(plan.extruder_colors[2] == file[2]);
        CHECK(plan.extruder_of_file_color[0] == 1);
        CHECK(plan.extruder_of_file_color[2] == 3);
        CHECK(plan.extruder_of_file_color[3] >= 1); // unused colors still get a valid extruder
        CHECK(plan.extruder_of_file_color[4] == 1); // orange -> red
    }

    SECTION("only used filaments take extruders")
    {
        const ExtruderPlan plan = plan_extruders(file, {false, true, false, false, true}, 4);
        REQUIRE(plan.extruder_colors.size() == 2);
        CHECK(plan.extruder_colors[0] == file[1]);
        CHECK(plan.extruder_colors[1] == file[4]);
        CHECK(plan.extruder_of_file_color[1] == 1);
        CHECK(plan.extruder_of_file_color[4] == 2);
    }

    SECTION("more colors than extruders")
    {
        const ExtruderPlan plan = plan_extruders(file, {true, true, true, true, true}, 2);
        REQUIRE(plan.extruder_colors.size() == 2);
        CHECK(plan.extruder_of_file_color[3] == 2); // yellow is closer to green than to red
        CHECK(plan.extruder_of_file_color[4] == 1); // orange is closer to red
    }

    SECTION("single extruder")
    {
        const ExtruderPlan plan = plan_extruders(file, {true, true, true, true, true}, 1);
        for (const unsigned extruder : plan.extruder_of_file_color)
            CHECK(extruder == 1);
    }

    SECTION("degenerate inputs")
    {
        CHECK(plan_extruders(file, {}, 3).extruder_colors.size() == 3);
        CHECK(plan_extruders(file, {false, false, false, false, false}, 4).extruder_colors.empty());
        CHECK(plan_extruders(file, {}, 0).extruder_colors.empty());
        CHECK(plan_extruders({}, {}, 4).extruder_of_file_color.empty());
    }
}
