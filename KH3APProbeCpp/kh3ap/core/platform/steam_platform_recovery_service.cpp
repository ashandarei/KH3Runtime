#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/core/platform/steam_platform_recovery_service.h"

#include <polyhook2/Detour/x64Detour.hpp>

#include "kh3ap/core/platform/steam_language.h"
#include "kh3ap/core/platform/steam_query_port_recovery.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#define NOMINMAX
#include <WinSock2.h>
#include <windows.h>
#include <Xinput.h>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto steam_subsystem_query_port_offset = 0x1d8;

        constexpr auto steam_manifest_read_buffer_bytes = 4096;
        constexpr auto max_steam_query_recovery_attempts = 8;
        constexpr auto steam_server_query_ready_offset = 0x1C9;
        constexpr size_t maximum_steam_string_characters = 64;
        constexpr size_t executable_path_buffer_characters = 32768;
        constexpr size_t maximum_manifest_parent_depth = 10;

        constexpr size_t s_supported_steam_image_size = kh3ap::native_build::steam.image_size;
        constexpr uint32_t s_supported_steam_image_timestamp = kh3ap::native_build::steam.timestamp;
        constexpr uintptr_t s_steam_subsystem_init_rva = 0x563E950;

        constexpr std::array<std::byte, 18> s_steam_subsystem_init_signature{
            std::byte{0x40}, std::byte{0x53}, std::byte{0x48}, std::byte{0x83}, std::byte{0xEC},
            std::byte{0x30}, std::byte{0x48}, std::byte{0xC7}, std::byte{0x44}, std::byte{0x24},
            std::byte{0x20}, std::byte{0xFE}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
            std::byte{0x48}, std::byte{0x8B}, std::byte{0xD9},
        };
        constexpr uintptr_t s_kh3_steam_game_server_init_rva = 0x563EB40;

        constexpr std::array<std::byte, 22> s_kh3_steam_game_server_init_signature{
            std::byte{0x48}, std::byte{0x8B}, std::byte{0xC4}, std::byte{0x55}, std::byte{0x57},
            std::byte{0x41}, std::byte{0x54}, std::byte{0x41}, std::byte{0x56}, std::byte{0x41},
            std::byte{0x57}, std::byte{0x48}, std::byte{0x8D}, std::byte{0x68}, std::byte{0xA8},
            std::byte{0x48}, std::byte{0x81}, std::byte{0xEC}, std::byte{0x30}, std::byte{0x01},
            std::byte{0x00}, std::byte{0x00},
        };
        constexpr std::wstring_view s_steam_app_manifest_name = L"appmanifest_2552450.acf";
        constexpr size_t s_max_steam_app_manifest_bytes = 1024 * 1024;

        constexpr std::wstring_view s_internationalization_default_object_path =
            STR("/Script/Engine.Default__KismetInternationalizationLibrary");
        constexpr std::wstring_view s_get_current_culture_path =
            STR("/Script/Engine.KismetInternationalizationLibrary:GetCurrentCulture");

        RuntimeSteamPlatformRecoveryService* s_steam_subsystem_init_recovery_owner = nullptr;
        using SteamSubsystemInitFn = bool(__fastcall*)(void*, bool, uint32_t);
        using Kh3SteamGameServerInitFn = bool(__fastcall*)(void*);
        using SteamGameServerInitFn = bool (*)(uint32_t, uint16_t, uint16_t, uint16_t, int32_t,
                                               const char*);

        template <typename Function>
        auto steam_export(HMODULE steam_api, const char* name) -> Function
        {
            return steam_api ? reinterpret_cast<Function>(GetProcAddress(steam_api, name))
                             : nullptr;
        }

        auto current_executable_is_supported_steam_image() noexcept -> bool
        {
            HMODULE const module = GetModuleHandleW(nullptr);
            if (!module)
            {
                return false;
            }

            const auto* const base = reinterpret_cast<const std::byte*>(module);
            const auto* const dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos_header->e_magic != IMAGE_DOS_SIGNATURE || dos_header->e_lfanew <= 0)
            {
                return false;
            }
            const auto* const nt_headers =
                reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos_header->e_lfanew);
            return nt_headers->Signature == IMAGE_NT_SIGNATURE &&
                   nt_headers->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
                   nt_headers->OptionalHeader.SizeOfImage == s_supported_steam_image_size &&
                   nt_headers->FileHeader.TimeDateStamp == s_supported_steam_image_timestamp;
        }

        auto safe_steam_string(const char* value) -> std::string
        {
            if (!value)
            {
                return {};
            }
            std::string result{value};
            if (result.size() > maximum_steam_string_characters)
            {
                result.resize(maximum_steam_string_characters);
            }
            return result;
        }

        auto widen_ascii(std::string_view value) -> std::wstring
        {
            std::wstring result{};
            result.reserve(value.size());
            for (const unsigned char character : value)
            {
                result.push_back(static_cast<wchar_t>(character));
            }
            return result;
        }

        auto try_read_configured_game_language() -> std::optional<std::string>
        {
            std::array<wchar_t, executable_path_buffer_characters> executable_path{};
            const DWORD length = GetModuleFileNameW(nullptr, executable_path.data(),
                                                    static_cast<DWORD>(executable_path.size()));
            if (length == 0 || length >= executable_path.size())
            {
                return std::nullopt;
            }

            std::filesystem::path directory =
                std::filesystem::path{executable_path.data()}.parent_path();
            std::filesystem::path manifest_path{};
            for (size_t depth = 0; depth < maximum_manifest_parent_depth && !directory.empty();
                 ++depth)
            {
                const std::filesystem::path candidate = directory / s_steam_app_manifest_name;
                std::error_code exists_error{};
                if (std::filesystem::is_regular_file(candidate, exists_error) && !exists_error)
                {
                    manifest_path = candidate;
                    break;
                }
                const std::filesystem::path parent = directory.parent_path();
                if (parent == directory)
                {
                    break;
                }
                directory = parent;
            }
            if (manifest_path.empty())
            {
                return std::nullopt;
            }

            std::ifstream stream{manifest_path, std::ios::binary};
            if (!stream)
            {
                return std::nullopt;
            }

            std::string manifest{};
            std::array<char, steam_manifest_read_buffer_bytes> buffer{};
            while (stream)
            {
                stream.read(buffer.data(), buffer.size());
                const std::streamsize count = stream.gcount();
                if (count <= 0)
                {
                    break;
                }
                if (manifest.size() + static_cast<size_t>(count) > s_max_steam_app_manifest_bytes)
                {
                    return std::nullopt;
                }
                manifest.append(buffer.data(), static_cast<size_t>(count));
            }

            return steam_app_language_from_manifest(manifest);
        }

        auto capture_xinput_connected_mask() -> uint32_t
        {
            HMODULE const xinput = GetModuleHandleW(L"XINPUT1_3.dll");
            using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
            const auto get_state =
                xinput
                    ? reinterpret_cast<XInputGetStateFn>(GetProcAddress(xinput, "XInputGetState"))
                    : nullptr;
            if (!get_state)
            {
                return 0;
            }

            uint32_t mask = 0;
            for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index)
            {
                XINPUT_STATE state{};
                if (get_state(index, &state) == ERROR_SUCCESS)
                {
                    mask |= (1u << index);
                }
            }
            return mask;
        }

        auto capture_platform_snapshot() -> RuntimeSteamPlatformSnapshot
        {
            RuntimeSteamPlatformSnapshot snapshot{};
            snapshot.supported_steam_image = current_executable_is_supported_steam_image();
            snapshot.xinput_connected_mask = capture_xinput_connected_mask();
            if (snapshot.supported_steam_image)
            {
                snapshot.configured_game_language =
                    try_read_configured_game_language().value_or("");
            }

            HMODULE const steam_api = GetModuleHandleW(L"steam_api64.dll");
            snapshot.steam_api_loaded = steam_api != nullptr;
            if (!steam_api)
            {
                return snapshot;
            }

            using IsSteamRunningFn = bool (*)();
            using GetHSteamUserFn = int32_t (*)();
            using SteamAppsFn = void* (*)();
            using AppsGetLanguageFn = const char* (*)(void*);
            using SteamUtilsFn = void* (*)();
            using UtilsGetAppIdFn = uint32_t (*)(void*);
            using UtilsGetLanguageFn = const char* (*)(void*);
            using SteamUserFn = void* (*)();
            using UserLoggedOnFn = bool (*)(void*);
            using UserGetSteamIdFn = uint64_t (*)(void*);

            if (const auto is_running =
                    steam_export<IsSteamRunningFn>(steam_api, "SteamAPI_IsSteamRunning"))
            {
                snapshot.steam_running = is_running();
            }
            if (const auto get_user =
                    steam_export<GetHSteamUserFn>(steam_api, "SteamAPI_GetHSteamUser"))
            {
                snapshot.steam_user_handle = get_user();
            }

            if (const auto get_apps =
                    steam_export<SteamAppsFn>(steam_api, "SteamAPI_SteamApps_v008"))
            {
                if (void* const apps = get_apps())
                {
                    if (const auto get_language = steam_export<AppsGetLanguageFn>(
                            steam_api, "SteamAPI_ISteamApps_GetCurrentGameLanguage"))
                    {
                        snapshot.game_language = safe_steam_string(get_language(apps));
                    }
                }
            }

            if (const auto get_utils =
                    steam_export<SteamUtilsFn>(steam_api, "SteamAPI_SteamUtils_v010"))
            {
                if (void* const utils = get_utils())
                {
                    if (const auto get_app_id = steam_export<UtilsGetAppIdFn>(
                            steam_api, "SteamAPI_ISteamUtils_GetAppID"))
                    {
                        snapshot.app_id = get_app_id(utils);
                    }
                    if (const auto get_language = steam_export<UtilsGetLanguageFn>(
                            steam_api, "SteamAPI_ISteamUtils_GetSteamUILanguage"))
                    {
                        snapshot.ui_language = safe_steam_string(get_language(utils));
                    }
                }
            }

            if (const auto get_steam_user =
                    steam_export<SteamUserFn>(steam_api, "SteamAPI_SteamUser_v021"))
            {
                if (void* const user = get_steam_user())
                {
                    if (const auto is_logged_on = steam_export<UserLoggedOnFn>(
                            steam_api, "SteamAPI_ISteamUser_BLoggedOn"))
                    {
                        snapshot.logged_on = is_logged_on(user);
                    }
                    if (const auto get_steam_id = steam_export<UserGetSteamIdFn>(
                            steam_api, "SteamAPI_ISteamUser_GetSteamID"))
                    {
                        snapshot.steam_id = get_steam_id(user);
                    }
                }
            }

            return snapshot;
        }

        auto read_current_process_bytes(uintptr_t address, void* destination, size_t size) noexcept
            -> bool
        {
            if (address == 0 || !destination || size == 0)
            {
                return false;
            }
            SIZE_T bytes_read = 0;
            return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                     destination, size, &bytes_read) != FALSE &&
                   bytes_read == size;
        }

        auto choose_available_udp_query_port(bool supported_steam_image,
                                             bool steam_client_initialized, uint32_t app_id,
                                             uint16_t requested_port, uint16_t game_port)
            -> RuntimeSteamQueryPortDecision
        {
            if (!supported_steam_image || !steam_client_initialized ||
                app_id != s_kh3_steam_app_id || requested_port == 0)
            {
                return decide_runtime_steam_query_port(
                    supported_steam_image, steam_client_initialized, app_id, requested_port,
                    game_port, false, std::nullopt);
            }

            WSADATA winsock_data{};
            if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0)
            {
                return decide_runtime_steam_query_port(
                    supported_steam_image, steam_client_initialized, app_id, requested_port,
                    game_port, false, std::nullopt);
            }
            const auto cleanup = []()
            {
                WSACleanup();
            };
            const auto open_socket = []() -> SOCKET
            {
                SOCKET const socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (socket_handle != INVALID_SOCKET)
                {
                    BOOL exclusive = TRUE;
                    setsockopt(socket_handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                               reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
                }
                return socket_handle;
            };
            const auto bind_port = [](SOCKET socket_handle, uint16_t port) -> bool
            {
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = htonl(INADDR_ANY);
                address.sin_port = htons(port);
                return bind(socket_handle, reinterpret_cast<const sockaddr*>(&address),
                            sizeof(address)) == 0;
            };

            SOCKET requested_socket = open_socket();
            if (requested_socket == INVALID_SOCKET)
            {
                cleanup();
                return decide_runtime_steam_query_port(
                    supported_steam_image, steam_client_initialized, app_id, requested_port,
                    game_port, false, std::nullopt);
            }
            if (bind_port(requested_socket, requested_port))
            {
                closesocket(requested_socket);
                cleanup();
                return decide_runtime_steam_query_port(
                    supported_steam_image, steam_client_initialized, app_id, requested_port,
                    game_port, true, std::nullopt);
            }
            closesocket(requested_socket);

            for (size_t attempt = 0; attempt < max_steam_query_recovery_attempts; ++attempt)
            {
                SOCKET const candidate_socket = open_socket();
                if (candidate_socket == INVALID_SOCKET)
                {
                    break;
                }
                if (!bind_port(candidate_socket, 0))
                {
                    closesocket(candidate_socket);
                    continue;
                }
                sockaddr_in selected_address{};
                int selected_address_size = sizeof(selected_address);
                const bool selected =
                    getsockname(candidate_socket, reinterpret_cast<sockaddr*>(&selected_address),
                                &selected_address_size) == 0;
                const uint16_t candidate_port = selected ? ntohs(selected_address.sin_port) : 0;
                closesocket(candidate_socket);
                if (candidate_port == 0 || candidate_port == requested_port ||
                    candidate_port == game_port)
                {
                    continue;
                }
                cleanup();
                return decide_runtime_steam_query_port(
                    supported_steam_image, steam_client_initialized, app_id, requested_port,
                    game_port, false, candidate_port);
            }

            cleanup();
            return decide_runtime_steam_query_port(supported_steam_image, steam_client_initialized,
                                                   app_id, requested_port, game_port, false,
                                                   std::nullopt);
        }

        auto internationalization_default_object() -> UObject*
        {
            UObject* const object = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, s_internationalization_default_object_path.data());
            return is_uobject_valid_fast(object) ? object : nullptr;
        }

        auto try_get_current_culture(UObject* library) -> std::optional<std::wstring>
        {
            UFunction* const function = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, s_get_current_culture_path.data());
            FProperty* const return_property = function ? function->GetReturnProperty() : nullptr;
            if (!library || !function || !return_property ||
                return_property->GetClass().GetName() != STR("StrProperty") ||
                function->GetParmsSize() <= 0)
            {
                return std::nullopt;
            }

            std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()));
            function->InitializeStruct(params.data());
            library->ProcessEvent(function, params.data());

            std::optional<std::wstring> result{};
            if (FString* const value =
                    return_property->ContainerPtrToValuePtr<FString>(params.data()))
            {
                if (const wchar_t* const chars = value->GetCharArray())
                {
                    result = std::wstring{chars};
                }
            }
            function->DestroyStruct(params.data());
            return result;
        }

        auto log_platform_snapshot(std::wstring_view stage,
                                   const RuntimeSteamPlatformSnapshot& snapshot) -> void
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] SteamPlatformRecovery snapshot stage={} supported_image={} "
                    "api_loaded={} steam_running={} hsteamuser={} logged_on={} steam_id={} "
                    "app_id={} game_language={} configured_game_language={} ui_language={} "
                    "xinput_mask=0x{:X}\n"),
                stage, snapshot.supported_steam_image, snapshot.steam_api_loaded,
                snapshot.steam_running, snapshot.steam_user_handle, snapshot.logged_on,
                snapshot.steam_id, snapshot.app_id, widen_ascii(snapshot.game_language),
                widen_ascii(snapshot.configured_game_language), widen_ascii(snapshot.ui_language),
                snapshot.xinput_connected_mask);
        }

    } // namespace

    RuntimeSteamPlatformRecoveryService::RuntimeSteamPlatformRecoveryService(
        bool bypass_steam_game_server_init) noexcept
        : bypass_steam_game_server_init_{bypass_steam_game_server_init}
    {
    }

    RuntimeSteamPlatformRecoveryService::~RuntimeSteamPlatformRecoveryService()
    {
        uninstall_steam_game_server_init_recovery();
        uninstall_early_steam_subsystem_init_recovery();
    }

    auto RuntimeSteamPlatformRecoveryService::X64DetourDeleter::operator()(
        PLH::x64Detour* detour) const noexcept -> void
    {
        delete detour;
    }

    auto RuntimeSteamPlatformRecoveryService::capture_early_platform_state() -> void
    {
        early_snapshot_ = capture_platform_snapshot();
        if (early_snapshot_.supported_steam_image)
        {
            log_platform_snapshot(STR("cpp_mod_constructor"), early_snapshot_);
            install_early_steam_subsystem_init_recovery();
        }
    }

    auto RuntimeSteamPlatformRecoveryService::install_early_steam_subsystem_init_recovery() -> void
    {
        if (steam_subsystem_init_hook_attempted_ || steam_subsystem_init_hook_installed_)
        {
            return;
        }
        steam_subsystem_init_hook_attempted_ = true;

        HMODULE const main_module = GetModuleHandleW(nullptr);
        const uintptr_t main_base = reinterpret_cast<uintptr_t>(main_module);
        const uintptr_t init_address = main_base != 0 ? main_base + s_steam_subsystem_init_rva : 0;
        std::array<std::byte, s_steam_subsystem_init_signature.size()> observed_signature{};
        const bool signature_ok =
            read_current_process_bytes(init_address, observed_signature.data(),
                                       observed_signature.size()) &&
            observed_signature == s_steam_subsystem_init_signature;
        if (!signature_ok)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] SteamPlatformRecovery subsystem hook skipped "
                    "reason=signature_mismatch rva=0x{:X}\n"),
                s_steam_subsystem_init_rva);
            return;
        }

        s_steam_subsystem_init_recovery_owner = this;
        steam_subsystem_init_detour_.reset(new PLH::x64Detour(
            init_address,
            reinterpret_cast<uint64_t>(
                &RuntimeSteamPlatformRecoveryService::hooked_steam_subsystem_init),
            &steam_subsystem_init_trampoline_));
        steam_subsystem_init_hook_installed_ = steam_subsystem_init_detour_->hook();
        if (!steam_subsystem_init_hook_installed_)
        {
            steam_subsystem_init_detour_.reset();
            steam_subsystem_init_trampoline_ = 0;
            s_steam_subsystem_init_recovery_owner = nullptr;
        }
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] SteamPlatformRecovery subsystem hook installed={} rva=0x{:X}\n"),
            steam_subsystem_init_hook_installed_, s_steam_subsystem_init_rva);
    }

    auto RuntimeSteamPlatformRecoveryService::uninstall_early_steam_subsystem_init_recovery()
        -> void
    {
        if (steam_subsystem_init_detour_)
        {
            if (steam_subsystem_init_hook_installed_)
            {
                steam_subsystem_init_detour_->unHook();
            }
            steam_subsystem_init_detour_.reset();
        }
        steam_subsystem_init_hook_installed_ = false;
        steam_subsystem_init_trampoline_ = 0;
        if (s_steam_subsystem_init_recovery_owner == this)
        {
            s_steam_subsystem_init_recovery_owner = nullptr;
        }
    }

    auto RuntimeSteamPlatformRecoveryService::install_steam_game_server_init_recovery() -> void
    {
        if (steam_game_server_init_hook_attempted_ || steam_game_server_init_hook_installed_)
        {
            return;
        }
        steam_game_server_init_hook_attempted_ = true;

        if (bypass_steam_game_server_init_)
        {
            HMODULE const main_module = GetModuleHandleW(nullptr);
            const uintptr_t main_base = reinterpret_cast<uintptr_t>(main_module);
            const uintptr_t init_address =
                main_base != 0 ? main_base + s_kh3_steam_game_server_init_rva : 0;
            std::array<std::byte, s_kh3_steam_game_server_init_signature.size()>
                observed_signature{};
            const bool signature_ok =
                read_current_process_bytes(init_address, observed_signature.data(),
                                           observed_signature.size()) &&
                observed_signature == s_kh3_steam_game_server_init_signature;
            if (!signature_ok)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] SteamPlatformRecovery client-only game-server bypass "
                        "skipped reason=signature_mismatch rva=0x{:X}\n"),
                    s_kh3_steam_game_server_init_rva);
                return;
            }

            steam_game_server_init_detour_.reset(new PLH::x64Detour(
                init_address,
                reinterpret_cast<uint64_t>(
                    &RuntimeSteamPlatformRecoveryService::hooked_kh3_steam_game_server_init),
                &steam_game_server_init_trampoline_));
            steam_game_server_init_hook_installed_ = steam_game_server_init_detour_->hook();
            if (!steam_game_server_init_hook_installed_)
            {
                steam_game_server_init_detour_.reset();
                steam_game_server_init_trampoline_ = 0;
            }
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] SteamPlatformRecovery client-only "
                                                "game-server bypass installed={} rva=0x{:X}\n"),
                                            steam_game_server_init_hook_installed_,
                                            s_kh3_steam_game_server_init_rva);
            return;
        }

        HMODULE const steam_api = GetModuleHandleW(L"steam_api64.dll");
        void* const init_address = steam_api ? reinterpret_cast<void*>(GetProcAddress(
                                                   steam_api, "SteamInternal_GameServer_Init"))
                                             : nullptr;
        if (!init_address)
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] SteamPlatformRecovery game-server "
                                                "recovery skipped reason=export_unavailable\n"));
            return;
        }

        steam_game_server_init_detour_.reset(new PLH::x64Detour(
            reinterpret_cast<uint64_t>(init_address),
            reinterpret_cast<uint64_t>(
                &RuntimeSteamPlatformRecoveryService::hooked_steam_game_server_init),
            &steam_game_server_init_trampoline_));
        steam_game_server_init_hook_installed_ = steam_game_server_init_detour_->hook();
        if (!steam_game_server_init_hook_installed_)
        {
            steam_game_server_init_detour_.reset();
            steam_game_server_init_trampoline_ = 0;
        }
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] SteamPlatformRecovery game-server recovery installed={}\n"),
            steam_game_server_init_hook_installed_);
    }

    auto RuntimeSteamPlatformRecoveryService::uninstall_steam_game_server_init_recovery() -> void
    {
        if (steam_game_server_init_detour_)
        {
            if (steam_game_server_init_hook_installed_)
            {
                steam_game_server_init_detour_->unHook();
            }
            steam_game_server_init_detour_.reset();
        }
        steam_game_server_init_hook_installed_ = false;
        steam_game_server_init_trampoline_ = 0;
    }

    bool __fastcall RuntimeSteamPlatformRecoveryService::hooked_kh3_steam_game_server_init(
        void* manager)
    {
        RuntimeSteamPlatformRecoveryService* const owner = s_steam_subsystem_init_recovery_owner;
        if (!owner || owner->steam_game_server_init_trampoline_ == 0)
        {
            return false;
        }

        const auto original =
            reinterpret_cast<Kh3SteamGameServerInitFn>(owner->steam_game_server_init_trampoline_);
        const bool expected_call = manager && manager == owner->active_steam_subsystem_manager_ &&
                                   owner->early_snapshot_.supported_steam_image &&
                                   owner->steam_client_init_succeeded_ &&
                                   owner->active_steam_subsystem_app_id_ == s_kh3_steam_app_id;
        if (!expected_call)
        {
            return original(manager);
        }

        // KH3's normal method sets this byte only after the real Steam game
        // server and every required server interface exist. Leave it false so
        // later ticks and shutdown skip all SteamGameServer operations, while
        // returning success to the outer platform initializer so it retains
        // the already-valid Steam client context.
        const uint8_t server_initialized = 0;
        SIZE_T bytes_written = 0;
        const bool server_flag_cleared =
            WriteProcessMemory(GetCurrentProcess(),
                               static_cast<std::byte*>(manager) + steam_server_query_ready_offset,
                               &server_initialized, sizeof(server_initialized),
                               &bytes_written) != FALSE &&
            bytes_written == sizeof(server_initialized);
        if (!server_flag_cleared)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] SteamPlatformRecovery client-only game-server bypass fallback "
                    "reason=server_flag_write_failed original_called=true\n"));
            return original(manager);
        }

        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] SteamPlatformRecovery client-only game-server bypass result=true "
                "original_called=false server_initialized=false\n"));

        owner->active_steam_subsystem_manager_ = nullptr;
        owner->active_steam_subsystem_app_id_ = 0;
        owner->steam_client_init_succeeded_ = false;
        return true;
    }

    bool RuntimeSteamPlatformRecoveryService::hooked_steam_game_server_init(
        uint32_t ip, uint16_t legacy_steam_port, uint16_t game_port, uint16_t query_port,
        int32_t server_mode, const char* version_string)
    {
        RuntimeSteamPlatformRecoveryService* const owner = s_steam_subsystem_init_recovery_owner;
        if (!owner || owner->steam_game_server_init_trampoline_ == 0)
        {
            return false;
        }
        const auto original =
            reinterpret_cast<SteamGameServerInitFn>(owner->steam_game_server_init_trampoline_);
        const RuntimeSteamQueryPortDecision port_decision = choose_available_udp_query_port(
            owner->early_snapshot_.supported_steam_image, owner->steam_client_init_succeeded_,
            owner->active_steam_subsystem_app_id_, query_port, game_port);
        const bool result = original(ip, legacy_steam_port, game_port, port_decision.effective_port,
                                     server_mode, version_string);
        bool manager_port_updated = false;
        if (result && port_decision.replaced && owner->active_steam_subsystem_manager_)
        {
            const uint32_t manager_query_port = port_decision.effective_port;
            SIZE_T bytes_written = 0;
            manager_port_updated =
                WriteProcessMemory(GetCurrentProcess(),
                                   static_cast<std::byte*>(owner->active_steam_subsystem_manager_) +
                                       steam_subsystem_query_port_offset,
                                   &manager_query_port, sizeof(manager_query_port),
                                   &bytes_written) != FALSE &&
                bytes_written == sizeof(manager_query_port);
        }
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] SteamPlatformRecovery game-server init result={} ip=0x{:08X} "
                "legacy_steam_port={} game_port={} requested_query_port={} effective_query_port={} "
                "query_port_replaced={} manager_port_updated={} server_mode={} version={}\n"),
            result, ip, legacy_steam_port, game_port, query_port, port_decision.effective_port,
            port_decision.replaced, manager_port_updated, server_mode,
            widen_ascii(safe_steam_string(version_string)));
        owner->active_steam_subsystem_manager_ = nullptr;
        owner->active_steam_subsystem_app_id_ = 0;
        owner->steam_client_init_succeeded_ = false;
        return result;
    }

    bool __fastcall RuntimeSteamPlatformRecoveryService::hooked_steam_subsystem_init(
        void* manager, bool relaunch_in_steam, uint32_t app_id)
    {
        RuntimeSteamPlatformRecoveryService* const owner = s_steam_subsystem_init_recovery_owner;
        if (!owner || owner->steam_subsystem_init_trampoline_ == 0)
        {
            return false;
        }
        return owner->handle_steam_subsystem_init(manager, relaunch_in_steam, app_id);
    }

    auto RuntimeSteamPlatformRecoveryService::handle_steam_subsystem_init(void* manager,
                                                                          bool relaunch_in_steam,
                                                                          uint32_t app_id) -> bool
    {
        const auto original =
            reinterpret_cast<SteamSubsystemInitFn>(steam_subsystem_init_trampoline_);
        active_steam_subsystem_manager_ = manager;
        active_steam_subsystem_app_id_ = app_id;
        const bool original_result = original(manager, relaunch_in_steam, app_id);
        steam_client_init_succeeded_ = original_result;
        if (original_result && early_snapshot_.supported_steam_image &&
            app_id == s_kh3_steam_app_id)
        {
            install_steam_game_server_init_recovery();
        }
        const bool recovery_armed = original_result && early_snapshot_.supported_steam_image &&
                                    app_id == s_kh3_steam_app_id &&
                                    steam_game_server_init_hook_installed_;
        if (!recovery_armed)
        {
            active_steam_subsystem_manager_ = nullptr;
            active_steam_subsystem_app_id_ = 0;
            steam_client_init_succeeded_ = false;
        }
        const RuntimeSteamPlatformSnapshot snapshot = capture_platform_snapshot();
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] SteamPlatformRecovery subsystem init result={} relaunch={} "
                "requested_app_id={} game_server_intercept_armed={} client_only_mode={} "
                "hsteamuser={} logged_on={} steam_id={} active_app_id={}\n"),
            original_result, relaunch_in_steam, app_id, recovery_armed,
            bypass_steam_game_server_init_, snapshot.steam_user_handle, snapshot.logged_on,
            snapshot.steam_id, snapshot.app_id);
        return original_result;
    }

    auto RuntimeSteamPlatformRecoveryService::observe_unreal_startup_state() -> void
    {
        RuntimeSteamPlatformSnapshot snapshot = capture_platform_snapshot();
        if (!snapshot.supported_steam_image)
        {
            return;
        }
        if (snapshot.configured_game_language.empty())
        {
            snapshot.configured_game_language = early_snapshot_.configured_game_language;
        }
        log_platform_snapshot(STR("on_unreal_init"), snapshot);

        UObject* const library = internationalization_default_object();
        const std::optional<std::wstring> current = try_get_current_culture(library);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] SteamPlatformRecovery culture observed current={} library={} "
                "mutation=false authority=vanilla_steam\n"),
            current.value_or(STR("<unavailable>")), library != nullptr);
    }

} // namespace kh3ap
