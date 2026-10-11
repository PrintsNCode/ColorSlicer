#pragma once

// Import of print settings stored in 3MF files made by other slicers
// (Bambu Studio, OrcaSlicer, Anycubic Slicer Next, ...).
//
// Flow:  Metadata/project_settings.config (JSON)
//        -> parse_project_settings_json()   foreign key -> string
//        -> translate()                     PrusaSlicer key -> serialized value
//        -> apply()                         written into the selected print preset values,
//                                           so PrusaSlicer shows them as "modified" and
//                                           the user can revert each one.

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <miniz.h> // mz_zip_archive

namespace Slic3r::Domain {
class ConfigContainer;
struct ConfigBox;
}

namespace Slic3r::Biz::Format::ForeignPrint {

// PrusaSlicer option key + serialized value ("rear", "15%", "2", "1", ...).
using Settings = std::vector<std::pair<std::string, std::string>>;

struct ApplyReport
{
    std::vector<std::string> applied;  // keys that now differ from the preset
    std::vector<std::string> unchanged; // keys whose value already matched
    std::vector<std::string> skipped;  // unknown key / value not valid for this option
};

// Flattens the JSON object: strings, numbers and booleans are kept, arrays
// (per-filament / per-extruder data) and nested objects are ignored.
std::map<std::string, std::string> parse_project_settings_json(const std::string& json_text);

// Foreign key/value -> PrusaSlicer key/value. Unknown foreign keys are dropped.
Settings translate(const std::map<std::string, std::string>& foreign);

// Reads Metadata/project_settings.config from the archive. Empty when absent/invalid.
Settings read_from_archive(mz_zip_archive& archive);

// Writes the settings into the selected print preset values of the container
// (FDM only). Only values that really differ are changed.
ApplyReport apply(Domain::ConfigContainer& container, const Settings& settings);

// Same, directly on a config box (print settings box of a preset).
ApplyReport apply_to_box(Domain::ConfigBox& print_box, const Settings& settings);

} // namespace Slic3r::Biz::Format::ForeignPrint
