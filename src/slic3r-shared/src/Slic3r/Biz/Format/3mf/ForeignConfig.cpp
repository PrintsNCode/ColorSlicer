#include "ForeignConfig.hpp"

#include <charconv>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <expat.h>
#include <nlohmann/json.hpp>

namespace Slic3r::Biz::Format::ForeignConfig {

namespace {

constexpr const char* PROJECT_SETTINGS_FILE = "Metadata/project_settings.config";
constexpr const char* MODEL_SETTINGS_FILE   = "Metadata/model_settings.config";

std::optional<std::string> read_file(mz_zip_archive& archive, const char* name)
{
    const int index = mz_zip_reader_locate_file(&archive, name, nullptr, 0);
    if (index < 0)
        return std::nullopt;

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&archive, static_cast<mz_uint>(index), &stat))
        return std::nullopt;

    // Those are small text files. Refuse anything absurd instead of allocating it.
    constexpr mz_uint64 MAX_SIZE = 64ull * 1024 * 1024;
    if (stat.m_uncomp_size == 0 || stat.m_uncomp_size > MAX_SIZE)
        return std::nullopt;

    std::string data(static_cast<size_t>(stat.m_uncomp_size), '\0');
    if (!mz_zip_reader_extract_to_mem(&archive, static_cast<mz_uint>(index), data.data(), data.size(), 0))
        return std::nullopt;
    return data;
}

std::optional<int> parse_int(std::string_view text)
{
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

struct SettingsParser
{
    BambuColors* out;
    // Open <object>/<part> elements: id of the innermost one (-1 when none) and which kind it is.
    enum class Kind { None, Object, Part };
    std::vector<std::pair<Kind, int>> stack;
};

const char* attribute(const char** attributes, const char* name)
{
    for (; attributes[0] != nullptr; attributes += 2)
        if (std::string_view(attributes[0]) == name)
            return attributes[1];
    return nullptr;
}

void XMLCALL on_start(void* user_data, const char* name, const char** attributes)
{
    auto& parser = *static_cast<SettingsParser*>(user_data);
    const std::string_view tag(name);

    if (tag == "object" || tag == "part") {
        int id = -1;
        if (const char* id_text = attribute(attributes, "id"))
            id = parse_int(id_text).value_or(-1);
        parser.stack.emplace_back(
            tag == "object" ? SettingsParser::Kind::Object : SettingsParser::Kind::Part, id
        );
        return;
    }

    if (tag != "metadata" || parser.stack.empty())
        return;

    const char* key   = attribute(attributes, "key");
    const char* value = attribute(attributes, "value");
    if (key == nullptr || value == nullptr || std::string_view(key) != "extruder")
        return;

    const auto [kind, id] = parser.stack.back();
    const std::optional<int> extruder = parse_int(value);
    if (id < 0 || !extruder || *extruder < 1)
        return;

    if (kind == SettingsParser::Kind::Object)
        parser.out->object_extruder[id] = *extruder;
    else
        parser.out->part_extruder[id] = *extruder;
}

void XMLCALL on_end(void* user_data, const char* name)
{
    auto& parser = *static_cast<SettingsParser*>(user_data);
    const std::string_view tag(name);
    if ((tag == "object" || tag == "part") && !parser.stack.empty())
        parser.stack.pop_back();
}

} // namespace

std::vector<ColorImport::Rgba> parse_filament_colors(const std::string& project_settings_json)
{
    std::vector<ColorImport::Rgba> colors;
    const nlohmann::json json = nlohmann::json::parse(project_settings_json, nullptr, false);
    if (json.is_discarded() || !json.is_object())
        return colors;

    const auto it = json.find("filament_colour");
    if (it == json.end() || !it->is_array())
        return colors;

    for (const nlohmann::json& item : *it) {
        std::optional<ColorImport::Rgba> color;
        if (item.is_string())
            color = ColorImport::parse_color(item.get<std::string>());
        // Keep the position of the filament even when its color is unreadable.
        colors.push_back(color.value_or(ColorImport::Rgba{}));
    }
    return colors;
}

void parse_model_settings(const std::string& model_settings_xml, BambuColors& out)
{
    SettingsParser state{&out, {}};

    XML_Parser parser = XML_ParserCreate(nullptr);
    if (parser == nullptr)
        return;
    XML_SetUserData(parser, &state);
    XML_SetElementHandler(parser, on_start, on_end);
    // A broken file is not an error worth reporting: whatever was read before stays.
    XML_Parse(parser, model_settings_xml.data(), static_cast<int>(model_settings_xml.size()), 1);
    XML_ParserFree(parser);
}

BambuColors read_bambu_colors(mz_zip_archive& archive)
{
    BambuColors result;

    if (const auto text = read_file(archive, PROJECT_SETTINGS_FILE))
        result.filament_colors = parse_filament_colors(*text);

    if (const auto text = read_file(archive, MODEL_SETTINGS_FILE)) {
        result.has_model_settings = true;
        parse_model_settings(*text, result);
    }

    return result;
}

} // namespace Slic3r::Biz::Format::ForeignConfig
