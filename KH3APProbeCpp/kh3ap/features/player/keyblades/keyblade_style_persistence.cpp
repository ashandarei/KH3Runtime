#include "keyblade_style_persistence.h"
#include "keyblade_options_helpers.h"
#include "keyblade_style_catalog.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include <algorithm>
#include <sstream>
#include <vector>

namespace kh3ap::keyblade_options
{
    namespace
    {
        constexpr std::wstring_view s_style_state_filename = L"runtime_keyblade_styles.json";
    }

    auto load_persisted_styles(RuntimeKeybladeStyleState& state,
                               const std::optional<std::filesystem::path>& bridge_dir) -> void
    {
        if (state.loaded_from_disk || !bridge_dir.has_value())
        {
            return;
        }
        state.loaded_from_disk = true;

        const auto text =
            read_text_file(*bridge_dir / std::filesystem::path{s_style_state_filename});
        if (!text.has_value())
        {
            return;
        }

        for (const auto& name : JsonFieldReader::string_array_field(*text, "unlocked_styles"))
        {
            if (is_valid_keyblade_name(name))
            {
                state.unlocked_styles.insert(name);
            }
        }
    }

    auto save_persisted_styles(RuntimeKeybladeStyleState& state,
                               const std::optional<std::filesystem::path>& bridge_dir) -> void
    {
        if (!state.keyblade_save_pending || !bridge_dir.has_value())
        {
            return;
        }

        std::vector<std::wstring> names{state.unlocked_styles.begin(), state.unlocked_styles.end()};
        std::sort(names.begin(), names.end());

        std::ostringstream output;
        output << "{\"unlocked_styles\":[";
        for (size_t index = 0; index < names.size(); ++index)
        {
            if (index != 0)
            {
                output << ',';
            }
            output << '"' << to_utf8(names[index]) << '"';
        }
        output << "]}";

        if (write_text_file(*bridge_dir / std::filesystem::path{s_style_state_filename},
                            output.str()))
        {
            state.keyblade_save_pending = false;
        }
    }

} // namespace kh3ap::keyblade_options
