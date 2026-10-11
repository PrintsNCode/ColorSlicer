#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Format/3mf/ForeignPrint.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"

#include <algorithm>

using namespace Slic3r;
using namespace Slic3r::Biz::Format::ForeignPrint;

namespace {

std::string value_of(const Settings& settings, const std::string& key)
{
    for (const auto& [k, v] : settings) {
        if (k == key) {
            return v;
        }
    }
    return "<missing>";
}

bool contains(const std::vector<std::string>& keys, const std::string& key)
{
    return std::ranges::find(keys, key) != keys.end();
}

} // namespace

TEST_CASE("Foreign print settings: JSON parsing keeps scalars only", "[foreignprint]")
{
    const auto parsed = parse_project_settings_json(
        R"({"seam_position":"back","wall_loops":2,"enable_support":true,"layer_height":0.2,
            "filament_colour":["#FF0000"],"nested":{"a":1},"nothing":null})"
    );
    CHECK(parsed.at("seam_position") == "back");
    CHECK(parsed.at("wall_loops") == "2");
    CHECK(parsed.at("enable_support") == "1");
    CHECK(parsed.at("layer_height") == "0.2");
    CHECK(parsed.count("filament_colour") == 0);
    CHECK(parsed.count("nested") == 0);
    CHECK(parsed.count("nothing") == 0);
    CHECK(parse_project_settings_json("not json").empty());
}

TEST_CASE("Foreign print settings: Bambu/Orca values are translated", "[foreignprint]")
{
    const Settings s = translate({
        {"seam_position", "back"},
        {"wall_loops", "3"},
        {"top_shell_layers", "10"},
        {"sparse_infill_density", "15%"},
        {"sparse_infill_pattern", "zig-zag"},
        {"top_surface_pattern", "monotonicline"},
        {"outer_wall_speed", "60"},
        {"initial_layer_print_height", "0.25"},
        {"enable_prime_tower", "1"},
        {"wall_generator", "arachne"},
        {"fuzzy_skin", "allwalls"},
        {"wall_sequence", "inner wall/outer wall"},
        {"ensure_vertical_shell_thickness", "ensure_moderate"},
    });
    CHECK(value_of(s, "seam_position") == "rear");
    CHECK(value_of(s, "perimeters") == "3");
    CHECK(value_of(s, "top_solid_layers") == "10");
    CHECK(value_of(s, "fill_density") == "15%");
    CHECK(value_of(s, "fill_pattern") == "rectilinear");
    CHECK(value_of(s, "top_fill_pattern") == "monotoniclines");
    CHECK(value_of(s, "external_perimeter_speed") == "60");
    CHECK(value_of(s, "first_layer_height") == "0.25");
    CHECK(value_of(s, "wipe_tower") == "1");
    CHECK(value_of(s, "perimeter_generator") == "arachne");
    CHECK(value_of(s, "fuzzy_skin") == "all");
    CHECK(value_of(s, "external_perimeters_first") == "0");
    CHECK(value_of(s, "ensure_vertical_shell_thickness") == "partial");
}

TEST_CASE("Foreign print settings: supports", "[foreignprint]")
{
    SECTION("tree auto")
    {
        const Settings s = translate({{"enable_support", "1"}, {"support_type", "tree(auto)"}});
        CHECK(value_of(s, "support_material") == "everywhere");
        CHECK(value_of(s, "support_material_style") == "organic");
    }
    SECTION("normal manual snug")
    {
        const Settings s = translate(
            {{"enable_support", "1"}, {"support_type", "normal(manual)"}, {"support_style", "snug"}}
        );
        CHECK(value_of(s, "support_material") == "enforcers_only");
        CHECK(value_of(s, "support_material_style") == "snug");
    }
    SECTION("disabled")
    {
        const Settings s = translate({{"enable_support", "0"}, {"support_type", "tree(auto)"}});
        CHECK(value_of(s, "support_material") == "none");
        CHECK(value_of(s, "support_material_style") == "<missing>");
    }
}

TEST_CASE("Foreign print settings: ironing and brim", "[foreignprint]")
{
    CHECK(value_of(translate({{"ironing_type", "no ironing"}}), "ironing") == "0");

    const Settings ironing = translate({{"ironing_type", "topmost"}});
    CHECK(value_of(ironing, "ironing") == "1");
    CHECK(value_of(ironing, "ironing_type") == "topmost");

    const Settings brim = translate({{"brim_type", "auto_brim"}, {"brim_width", "5"}});
    CHECK(value_of(brim, "brim_type") == "outer_only");
    CHECK(value_of(brim, "brim_width") == "5");
}

TEST_CASE("Foreign print settings: unknown keys and values are dropped", "[foreignprint]")
{
    const Settings s = translate({
        {"some_orca_only_key", "1"},
        {"seam_position", "diagonal"},
        {"sparse_infill_pattern", "crosshatch"},
        {"wall_loops", ""},
    });
    CHECK(s.empty());
}

TEST_CASE("Foreign print settings: apply marks only real differences", "[foreignprint]")
{
    Domain::ConfigPackFDM pack{4};
    auto& box = pack.print;

    // Start from a known state.
    box.items.opt("perimeters").set(2);
    box.items.opt("seam_position").set(Domain::SeamPosition::spAligned);

    const Settings settings = {
        {"perimeters", "2"},           // same as preset
        {"seam_position", "rear"},     // enum
        {"fill_density", "35%"},       // percentage
        {"top_solid_layers", "9"},     // int
        {"travel_speed", "250"},       // double
        {"first_layer_height", "50%"}, // float or percent
        {"wipe_tower", "1"},           // bool
        {"seam_position_typo", "x"},   // unknown key
        {"fill_pattern", "nonsense"},  // invalid enum value
        {"bridge_speed", "fast"},      // not a number
    };
    const ApplyReport report = apply_to_box(box, settings);

    CHECK(contains(report.unchanged, "perimeters"));
    CHECK(contains(report.applied, "seam_position"));
    CHECK(contains(report.applied, "fill_density"));
    CHECK(contains(report.applied, "top_solid_layers"));
    CHECK(contains(report.applied, "travel_speed"));
    CHECK(contains(report.applied, "first_layer_height"));
    CHECK(contains(report.applied, "wipe_tower"));
    CHECK(contains(report.skipped, "seam_position_typo"));
    CHECK(contains(report.skipped, "fill_pattern"));
    CHECK(contains(report.skipped, "bridge_speed"));

    CHECK(box.items.opt("seam_position").get<Domain::SeamPosition>() == Domain::SeamPosition::spRear);
    CHECK(box.items.opt("top_solid_layers").get<int>() == 9);
    CHECK(box.items.opt("fill_density").get<Domain::Percentage>().value == 35.);
    CHECK(box.items.opt("travel_speed").get<double>() == 250.);
    CHECK(box.items.opt("wipe_tower").get<bool>());
}
