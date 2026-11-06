#include "kh3ap/core/platform/mom_socket_observer.h"
#include "kh3ap/core/input/controller_preferences_service.h"
#include "kh3ap/core/platform/mom_window_observer.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/utilities.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DynamicOutput/DynamicOutput.hpp>
#include <filesystem>
#include <string>
#include <mutex>
#include <cstdlib>

namespace kh3ap {
    namespace
    {
        constexpr auto windows_path_buffer_characters = 32768;
        constexpr auto first_unprivileged_activity_port = 1024;
        constexpr auto maximum_activity_port = 65535;
        constexpr auto activity_token_hex_characters = 32;
    }

auto make_controller_preferences_config() -> RuntimeControllerPreferencesConfig {
    wchar_t local_app_data[windows_path_buffer_characters]{};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, windows_path_buffer_characters);
    if (size == 0 || size >= windows_path_buffer_characters) return {};
    const auto path = std::filesystem::path{local_app_data} / L"KH3AP" / L"controller_preferences.json";
    return {
        .read_background_input = [path]() -> std::optional<bool> {
            const auto text = read_text_file(path);
            return text ? JsonFieldReader::bool_field(*text, "background_controller_input") : std::nullopt;
        },
        .write_background_input = [path](bool allowed) {
            return write_text_file_atomic_verified(path,
                std::string{"{\n  \"background_controller_input\": "} +
                (allowed ? "true" : "false") + "\n}\n");
        },
        .observe_melody_of_memory = [](std::function<void(bool)> activity) -> std::shared_ptr<void> {
            struct Sources {
                std::mutex mutex;
                bool windows{}, native{};
                std::function<void(bool)> activity;
                std::shared_ptr<void> window, socket;
                void update(bool external, bool active) {
                    std::lock_guard lock(mutex);
                    (external ? native : windows) = active;
                    activity(windows || native);
                }
            };
            auto sources = std::make_shared<Sources>();
            sources->activity = std::move(activity);
            char port_text[16]{};
            const auto length = GetEnvironmentVariableA("KH3AP_MOM_ACTIVITY_PORT", port_text, sizeof(port_text));
            std::optional<unsigned short> configured_port;
            std::string greeting;
            if (length && length < sizeof(port_text)) {
                char* end{};
                const auto port = strtol(port_text, &end, 10);
                if (end != port_text && !*end && port >= first_unprivileged_activity_port && port <= maximum_activity_port) {
                    configured_port = static_cast<unsigned short>(port);
                } else {
                    RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Invalid KH3AP_MOM_ACTIVITY_PORT; Linux activity listener disabled.\n"));
                }
            } else if (!length) {
                // AP-owned setup beside the actual executable, read once.
                // This works across Wine prefixes without changing launcher env.
                wchar_t executable[windows_path_buffer_characters]{};
                const auto count = GetModuleFileNameW(nullptr, executable, windows_path_buffer_characters);
                if (count && count < windows_path_buffer_characters) {
                    const auto config_path = std::filesystem::path{executable}.parent_path() / L"kh3ap_mom_activity.json";
                    if (const auto text = read_text_file(config_path)) {
                        const auto schema = JsonFieldReader::int64_field(*text, "schema");
                        const auto port = JsonFieldReader::int64_field(*text, "port");
                        const auto token = JsonFieldReader::string_field(*text, "token");
                        if (schema == 1 && port && *port >= first_unprivileged_activity_port && *port <= maximum_activity_port && token &&
                            token->size() == activity_token_hex_characters && token->find_first_not_of(L"0123456789abcdef") == std::wstring::npos) {
                            configured_port = static_cast<unsigned short>(*port);
                            greeting = "KH3AP_MOM_V1 ";
                            for (const auto character : *token) greeting.push_back(static_cast<char>(character));
                            greeting += '\n';
                        }
                    }
                }
            }
            if (configured_port) {
                try {
                    sources->socket = std::make_shared<RuntimeMoMSocketObserver>(
                        *configured_port, [p = sources.get()](bool active) { p->update(true, active); }, greeting);
                } catch (const std::exception&) {
                    RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] MoM Linux activity listener unavailable; check configured port.\n"));
                }
            }
            sources->window = std::make_shared<RuntimeMoMWindowObserver>(
                    [p = sources.get()](bool active) { p->update(false, active); },
                    [](const char* event, DWORD value) {
                        RC::Output::send<RC::LogLevel::Normal>(
                                STR("[KH3APProbeCpp] MoM event observer event={} value={}\n"),
                                std::wstring(event, event + std::char_traits<char>::length(event)), value);
                    });
            return sources;
        },
    };
}
} // namespace kh3ap
