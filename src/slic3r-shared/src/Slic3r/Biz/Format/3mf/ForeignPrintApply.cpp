#include "Slic3r/Biz/Format/3mf/ForeignPrint.hpp"

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/Percentage.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <optional>
#include <type_traits>

namespace Slic3r::Biz::Format::ForeignPrint {

namespace {

// ----------------------------------------------------------------------------
// Applying
// ----------------------------------------------------------------------------

std::optional<double> parse_double(const std::string& text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno != 0) {
        return std::nullopt;
    }
    return value;
}

// "15%" -> {15, true}; "0.4" -> {0.4, false}
std::optional<std::pair<double, bool>> parse_value_or_percent(std::string text)
{
    bool percent = false;
    if (!text.empty() && text.back() == '%') {
        percent = true;
        text.pop_back();
    }
    const auto value = parse_double(text);
    if (!value) {
        return std::nullopt;
    }
    return std::make_pair(*value, percent);
}

// Sets `item` from its serialized form. Returns false (item untouched) when the
// text is not valid for the option type.
bool assign_from_string(Domain::ConfigItem& item, const std::string& text)
{
    return item.visit([&](auto& value) -> bool {
        using V = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<V, bool>) {
            if (text == "1" || text == "true") {
                value = true;
                return true;
            }
            if (text == "0" || text == "false") {
                value = false;
                return true;
            }
            return false;
        } else if constexpr (std::is_same_v<V, int>) {
            const auto parsed = parse_double(text);
            if (!parsed) {
                return false;
            }
            value = static_cast<int>(*parsed + (*parsed < 0 ? -0.5 : 0.5));
            return true;
        } else if constexpr (std::is_same_v<V, double>) {
            const auto parsed = parse_double(text);
            if (!parsed) {
                return false;
            }
            value = *parsed;
            return true;
        } else if constexpr (std::is_same_v<V, Domain::Percentage>) {
            const auto parsed = parse_value_or_percent(text);
            if (!parsed) {
                return false;
            }
            value = Domain::Percentage{parsed->first};
            return true;
        } else if constexpr (std::is_same_v<V, Domain::FloatOrPercentage>) {
            const auto parsed = parse_value_or_percent(text);
            if (!parsed) {
                return false;
            }
            value = parsed->second ? Domain::FloatOrPercentage{Domain::Percentage{parsed->first}}
                                   : Domain::FloatOrPercentage{parsed->first};
            return true;
        } else if constexpr (std::is_same_v<V, Domain::EnumWrapper>) {
            const auto& defs = value.def();
            const bool known = std::any_of(defs.begin(), defs.end(), [&](const Domain::EnumValueDef& def) {
                return def.str_serialized == text;
            });
            if (!known) {
                return false;
            }
            value.set_string(text);
            return true;
        } else if constexpr (std::is_same_v<V, std::string>) {
            value = text;
            return true;
        } else {
            return false; // vectors, optionals, points: not part of the print settings we import
        }
    });
}

} // namespace

ApplyReport apply_to_box(Domain::ConfigBox& print, const Settings& settings)
{
    ApplyReport report;
    for (const auto& [key, text] : settings) {
        Domain::ConfigItem* item = print.items.find(key);
        if (item == nullptr) {
            report.skipped.push_back(key);
            continue;
        }
        Domain::ConfigItem candidate = *item;
        if (!assign_from_string(candidate, text)) {
            report.skipped.push_back(key);
            continue;
        }
        if (candidate == *item) {
            report.unchanged.push_back(key);
            continue;
        }
        *item = std::move(candidate);
        report.applied.push_back(key);
    }
    return report;
}

ApplyReport apply(Domain::ConfigContainer& container, const Settings& settings)
{
    if (container.print_technology() != Domain::PrinterTechnology::FFF) {
        return {};
    }
    auto& selected = container.mutable_selected_preset();
    ApplyReport report = apply_to_box(selected.print.config_box(), settings);

    // Tool presets can override print values (fill density, fill pattern, ...).
    // When one of them has the override switched on it would win over the value
    // we just set, so keep it in sync with the imported value.
    for (auto& tool : selected.tools) {
        Domain::ConfigBox& box = tool.config_box();
        for (const auto& [key, text] : settings) {
            Domain::ConfigItem* ov = box.overrides.find(key);
            if (ov == nullptr || !box.overrides.get(key).has_value()) {
                continue;
            }
            Domain::ConfigItem candidate = *ov;
            if (assign_from_string(candidate, text)) {
                *ov = std::move(candidate);
            }
        }
    }
    return report;
}

} // namespace Slic3r::Biz::Format::ForeignPrint
