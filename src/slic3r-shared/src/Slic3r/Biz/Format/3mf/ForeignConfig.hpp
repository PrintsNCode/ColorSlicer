#pragma once

#include <unordered_map>
#include <string>
#include <vector>

#include <miniz.h>

#include "Slic3r/Biz/Format/3mf/ColorImport.hpp"

namespace Slic3r::Biz::Format::ForeignConfig {

/**
 * What BambuStudio / OrcaSlicer store next to the model about filaments.
 * Both files are optional, a missing or unreadable one just leaves its part empty.
 */
struct BambuColors
{
    /// Metadata/project_settings.config: "filament_colour". Filament n (1-based) is item n - 1.
    std::vector<ColorImport::Rgba> filament_colors;
    /// Metadata/model_settings.config: <object id> / <metadata key="extruder">. 1-based filament.
    std::unordered_map<int, int> object_extruder;
    /// Metadata/model_settings.config: <part id> / <metadata key="extruder">. 1-based filament.
    std::unordered_map<int, int> part_extruder;
    /// True when model_settings.config exists, i.e. the file was written by a Bambu-like slicer.
    bool has_model_settings = false;
};

BambuColors read_bambu_colors(mz_zip_archive& archive);

/// Parts of read_bambu_colors() working on text, exposed for tests.
std::vector<ColorImport::Rgba> parse_filament_colors(const std::string& project_settings_json);
void parse_model_settings(const std::string& model_settings_xml, BambuColors& out);

} // namespace Slic3r::Biz::Format::ForeignConfig
