#include "kh02_companion_process.h"
#include "kh02_companion_windows.h"
#include "shared/kh02/kh02_asset_readiness.h"
#include "kh02_index_state.h"
#include "shared/kh02/kh02_executable_verification.h"
#include <nlohmann/json.hpp>
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include <objbase.h>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <vector>
#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <thread>

namespace kh3ap
{
    namespace
    {
        constexpr auto source_shutdown_timeout = 2000u;

        [[noreturn]] void fail(const char* operation)
        {
            throw std::runtime_error(std::string(operation) + ":" + std::to_string(GetLastError()));
        }

        auto utf8(std::wstring_view value) -> std::string
        {
            const auto count =
                WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                    nullptr, 0, nullptr, nullptr);
            std::string result(count, 0);
            WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                result.data(), count, nullptr, nullptr);
            return result;
        }

        auto desktop_name(HDESK desktop) -> std::wstring
        {
            DWORD required = 0;
            GetUserObjectInformationW(desktop, UOI_NAME, nullptr, 0, &required);
            if (!required)
            {
                return {};
            }
            std::vector<wchar_t> name(required / sizeof(wchar_t) + 1);
            if (!GetUserObjectInformationW(desktop, UOI_NAME, name.data(), required, &required))
            {
                return {};
            }
            return name.data();
        }

        struct WindowCounts
        {
            DWORD pid;
            unsigned all{}, visible{};
        };

        BOOL CALLBACK count_window(HWND window, LPARAM context)
        {
            auto& counts = *reinterpret_cast<WindowCounts*>(context);
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (pid == counts.pid)
            {
                ++counts.all;
                if (IsWindowVisible(window))
                {
                    ++counts.visible;
                }
            }
            return TRUE;
        }

        auto child_environment(const kh02control::Identity& identity, bool steam)
            -> std::vector<wchar_t>
        {
            auto* raw = GetEnvironmentStringsW();
            if (!raw)
            {
                fail("companion_environment");
            }
            std::vector<std::wstring> entries;
            for (const wchar_t* cursor = raw; *cursor; cursor += wcslen(cursor) + 1)
            {
                if (_wcsnicmp(cursor, L"SteamAppId=", 11) &&
                    _wcsnicmp(cursor, L"SteamGameId=", 12) &&
                    _wcsnicmp(cursor, L"KH02_CONTROL_", 13))
                {
                    entries.emplace_back(cursor);
                }
            }
            FreeEnvironmentStringsW(raw);
            if (steam)
            {
                entries.emplace_back(L"SteamAppId=2552440");
                entries.emplace_back(L"SteamGameId=2552440");
            }
            entries.emplace_back(L"KH02_CONTROL_OWNER=" + std::to_wstring(identity.owner_pid));
            entries.emplace_back(L"KH02_CONTROL_GENERATION=" +
                                 std::to_wstring(identity.session_generation));
            entries.emplace_back(L"KH02_CONTROL_NONCE=" +
                                 std::wstring(identity.nonce.begin(), identity.nonce.end()));
            std::sort(entries.begin(), entries.end(),
                      [](const auto& a, const auto& b)
                      {
                          return _wcsicmp(a.c_str(), b.c_str()) < 0;
                      });
            std::vector<wchar_t> result;
            for (const auto& entry : entries)
            {
                result.insert(result.end(), entry.begin(), entry.end());
                result.push_back(0);
            }
            result.push_back(0);
            return result;
        }

        void reject_existing_source()
        {
            const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE)
            {
                fail("companion_process_inventory");
            }
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            bool found = false;
            if (Process32FirstW(snapshot, &entry))
            {
                do
                {
                    if (!_wcsicmp(entry.szExeFile, L"KINGDOM HEARTS 0.2 Birth by Sleep.exe"))
                    {
                        found = true;
                        break;
                    }
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
            if (found)
            {
                throw std::runtime_error("companion_source_already_running");
            }
        }
    } // namespace

    struct Kh02CompanionProcess::Impl
    {
        std::mutex mutex;

        struct PublishedHealth
        {
            Kh02CompanionSession session;
            HANDLE process{};
            std::shared_ptr<kh02control::AssetReadinessSignal> assets_ready;

            ~PublishedHealth()
            {
                if (process)
                {
                    CloseHandle(process);
                }
            }
        };

        mutable std::atomic<std::shared_ptr<const PublishedHealth>> published_health;
        HANDLE job{}, process{};
        HDESK desktop{};
        DWORD pid{};
        bool executable_verified{};
        const kh02integrity::Build* source_build{};
        bool ready{};
        kh02control::Identity identity;
        uint64_t request_generation{};
        uint64_t launch_requested_ms{}, image_verified_ms{}, process_created_ms{},
            process_resumed_ms{};
        std::unique_ptr<kh02control::Client> control;
        std::shared_ptr<kh02control::AssetReadinessSignal> assets_ready;
        std::wstring desktop_id;
        bool wine{};
        std::unique_ptr<Kh02CompanionWindows> window_events;

        ~Impl()
        {
            clear();
        }

        void clear() noexcept
        {
            published_health.store(nullptr);
            control.reset();
            assets_ready.reset();
            ready = false;
            identity = {};
            // Close the only job handle first, exercising KILL_ON_JOB_CLOSE. A
            // failed pre-assignment launch is still our own suspended child.
            if (job)
            {
                CloseHandle(job);
                job = nullptr;
            }
            if (process)
            {
                if (WaitForSingleObject(process, source_shutdown_timeout) == WAIT_TIMEOUT)
                {
                    TerminateProcess(process, 0);
                    WaitForSingleObject(process, source_shutdown_timeout);
                }
                CloseHandle(process);
                process = nullptr;
            }
            if (desktop)
            {
                CloseDesktop(desktop);
                desktop = nullptr;
            }
            window_events.reset();
            pid = 0;
            executable_verified = false;
            source_build = nullptr;
            desktop_id.clear();
        }

        auto session() const -> Kh02CompanionSession
        {
            const auto input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
            const auto input_name = input ? desktop_name(input) : L"";
            if (input)
            {
                CloseDesktop(input);
            }
            const bool running = process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
            WindowCounts windows{pid};
            DWORD foreground_pid{};
            GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
            const bool hidden =
                wine && window_events &&
                EnumDesktopWindows(desktop, count_window, reinterpret_cast<LPARAM>(&windows)) &&
                windows.visible == 0 && foreground_pid != pid;
            const bool isolated = wine ? hidden : (!input_name.empty() && input_name != desktop_id);
            return {identity,
                    running,
                    job != nullptr,
                    executable_verified,
                    !input_name.empty(),
                    !input_name.empty() && input_name == desktop_id,
                    ready && running,
                    isolated};
        }

        auto health() const -> Kh02CompanionSession
        {
            const bool running = process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
            return {identity, running, job != nullptr,  executable_verified,
                    false,    false,   ready && running};
        }

        void publish_health() const
        {
            auto value = std::make_shared<PublishedHealth>();
            value->session = health();
            value->assets_ready = assets_ready;
            if (process && !DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(),
                                            &value->process, SYNCHRONIZE, FALSE, 0))
            {
                fail("companion_health_handle");
            }
            published_health.store(std::move(value));
        }

        auto status() -> std::string
        {
            const bool alive = process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
            WindowCounts windows{pid};
            const bool enumerated =
                desktop &&
                EnumDesktopWindows(desktop, count_window, reinterpret_cast<LPARAM>(&windows));
            const auto input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
            const auto input_name = input ? desktop_name(input) : L"";
            if (input)
            {
                CloseDesktop(input);
            }
            const bool input_known = !input_name.empty();
            nlohmann::json result = {
                {"owner_pid", GetCurrentProcessId()},
                {"control_protocol", kh02control::version},
                {"catalog_tag", identity.catalog_tag},
                {"session_generation", identity.session_generation},
                {"session_nonce", std::string(identity.nonce.begin(), identity.nonce.end())},
                {"ready", ready && alive},
                {"launch_requested_ms", launch_requested_ms},
                {"image_verified_ms", image_verified_ms},
                {"process_created_ms", process_created_ms},
                {"process_resumed_ms", process_resumed_ms},
                {"pid", pid},
                {"running", alive},
                {"owned", job != nullptr},
                {"executable_verified", executable_verified},
                {"executable_sha256", source_build ? source_build->executable_sha256 : ""},
                {"source_platform", source_build ? source_build->platform : ""},
                {"kill_on_owner_exit", job != nullptr},
                {"private_desktop", utf8(desktop_id)},
                {"window_isolation", wine ? "wine_window_events" : "private_desktop"},
                {"input_isolated", session().input_isolated},
                {"window_enumeration_ok", enumerated},
                {"window_count", windows.all},
                {"windows_marked_visible", windows.visible},
                {"input_desktop_known", input_known},
                {"input_desktop_matches",
                 input_known && !desktop_id.empty() && input_name == desktop_id},
                {"nullrhi_requested", true},
                {"nosound_requested", false},
                {"asset_service_requested", true},
                {"windows_audio_mute_requested", false},
                {"renderless_verified", false}};
            return result.dump();
        }
    };

    Kh02CompanionProcess::Kh02CompanionProcess() : impl(std::make_unique<Impl>())
    {
    }

    Kh02CompanionProcess::~Kh02CompanionProcess() = default;

    auto Kh02CompanionProcess::status() -> std::string
    {
        std::lock_guard lock(impl->mutex);
        return impl->status();
    }

    auto Kh02CompanionProcess::stop() -> std::string
    {
        std::lock_guard lock(impl->mutex);
        impl->clear();
        return impl->status();
    }

    auto Kh02CompanionProcess::start(std::wstring_view executable,
                                     const std::vector<std::wstring>& store_arguments)
        -> Kh02CompanionSession
    {
        std::lock_guard lock(impl->mutex);
        if (impl->process && WaitForSingleObject(impl->process, 0) == WAIT_TIMEOUT)
        {
            return impl->session();
        }
        impl->clear();
        reject_existing_source();
        impl->launch_requested_ms = GetTickCount64();
        const auto path = std::filesystem::absolute(std::filesystem::path(executable));
        if (path.filename() != L"KINGDOM HEARTS 0.2 Birth by Sleep.exe" ||
            !std::filesystem::is_regular_file(path))
        {
            throw std::runtime_error("companion_source_executable");
        }
        const auto executable_lock =
            CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                        FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (executable_lock == INVALID_HANDLE_VALUE)
        {
            kh02integrity::terminate_failed_verification("kh02_executable_lock_failed");
        }
        const std::unique_ptr<void, decltype(&CloseHandle)> locked_image(executable_lock,
                                                                         &CloseHandle);
        try
        {
            impl->source_build = &kh02integrity::verify_executable(path);
            kh02asset::configure_source_identity(impl->source_build->executable_sha256);
            impl->executable_verified = true;
            impl->image_verified_ms = GetTickCount64();
        }
        catch (const std::exception& error)
        {
            kh02integrity::terminate_failed_verification(error.what());
        }
        try
        {
            impl->desktop_id = L"KH02Assets_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                               std::to_wstring(GetTickCount64());
            impl->wine =
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
            if (impl->wine)
            {
                impl->desktop_id = L"Default";
            }
            impl->desktop =
                CreateDesktopW(impl->desktop_id.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
            if (!impl->desktop)
            {
                fail("companion_private_desktop");
            }
            impl->job = CreateJobObjectW(nullptr, nullptr);
            if (!impl->job)
            {
                fail("companion_job");
            }
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(impl->job, JobObjectExtendedLimitInformation, &limits,
                                         sizeof(limits)))
            {
                fail("companion_job_limits");
            }
            static uint64_t generation = 0;
            impl->identity.owner_pid = GetCurrentProcessId();
            impl->identity.catalog_tag = kh02asset::asset_index().tag;
            impl->identity.session_generation = ++generation;
            GUID nonce{};
            if (FAILED(CoCreateGuid(&nonce)))
            {
                throw std::runtime_error("companion_session_nonce");
            }
            constexpr char digits[] = "0123456789abcdef";
            const auto* nonce_bytes = reinterpret_cast<const unsigned char*>(&nonce);
            for (size_t index = 0; index < sizeof(nonce); ++index)
            {
                impl->identity.nonce[index * 2] = digits[nonce_bytes[index] >> 4];
                impl->identity.nonce[index * 2 + 1] = digits[nonce_bytes[index] & 15];
            }
            impl->assets_ready =
                std::make_shared<kh02control::AssetReadinessSignal>(impl->identity);
            const bool steam = impl->source_build == &kh02integrity::steam;
            auto environment = child_environment(impl->identity, steam);
            auto command = L"\"" + path.wstring() + L"\"" +
                           (steam ? std::wstring(L" -STEAM") : std::wstring{}) +
                           L" -epiclocale=en -nullrhi -unattended -nosplash -NoLoadingScreen "
                           L"-KH02AssetCompanion";
            for (const auto& argument : store_arguments)
            {
                const auto key = argument.substr(0, argument.find(L'='));
                static const std::vector<std::wstring> allowed{
                    L"-AUTH_LOGIN", L"-AUTH_PASSWORD", L"-AUTH_TYPE",    L"-epicapp",
                    L"-epicenv",    L"-EpicPortal",    L"-epicusername", L"-epicuserid",
                    L"-epiclocale", L"-epicsandboxid"};
                if (steam || std::find(allowed.begin(), allowed.end(), key) == allowed.end() ||
                    argument.find_first_of(L"\r\n") != std::wstring::npos ||
                    argument.find(L'\0') != std::wstring::npos)
                {
                    throw std::runtime_error("companion_store_argument_invalid");
                }
                command += L" \"";
                size_t slashes{};
                for (const auto character : argument)
                {
                    if (character == L'\\')
                    {
                        ++slashes;
                        continue;
                    }
                    command.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
                    command += character;
                    slashes = 0;
                }
                command.append(slashes * 2, L'\\');
                command += L'"';
            }
            auto desktop_path = L"WinSta0\\" + impl->desktop_id;
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.lpDesktop = desktop_path.data();
            startup.dwFlags = STARTF_USESHOWWINDOW;
            startup.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION child{};
            // Suspend before any game code runs so job assignment cannot race an
            // owner exit or let descendants escape lifetime ownership.
            if (!CreateProcessW(
                    path.c_str(), command.data(), nullptr, nullptr, FALSE,
                    CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                    environment.data(),
                    path.parent_path().parent_path().parent_path().parent_path().c_str(), &startup,
                    &child))
            {
                fail("companion_create_process");
            }
            impl->process = child.hProcess;
            impl->process_created_ms = GetTickCount64();
            impl->pid = child.dwProcessId;
            impl->identity.source_pid = child.dwProcessId;
            if (!AssignProcessToJobObject(impl->job, child.hProcess))
            {
                const auto error = GetLastError();
                CloseHandle(child.hThread);
                SetLastError(error);
                fail("companion_assign_job");
            }
            if (impl->wine)
            {
                try
                {
                    impl->window_events =
                        std::make_unique<Kh02CompanionWindows>(impl->pid, GetForegroundWindow());
                }
                catch (...)
                {
                    CloseHandle(child.hThread);
                    throw;
                }
            }
            if (ResumeThread(child.hThread) == static_cast<DWORD>(-1))
            {
                const auto error = GetLastError();
                CloseHandle(child.hThread);
                SetLastError(error);
                fail("companion_resume");
            }
            CloseHandle(child.hThread);
            impl->process_resumed_ms = GetTickCount64();
            impl->control = std::make_unique<kh02control::Client>();
            impl->publish_health();
            return impl->session();
        }
        catch (...)
        {
            impl->clear();
            throw;
        }
    }

    auto Kh02CompanionProcess::session() -> Kh02CompanionSession
    {
        std::lock_guard lock(impl->mutex);
        return impl->session();
    }

    auto Kh02CompanionProcess::health() const -> Kh02CompanionSession
    {
        const auto health = impl->published_health.load();
        if (!health)
        {
            return {};
        }
        auto session = health->session;
        session.running =
            health->process && WaitForSingleObject(health->process, 0) == WAIT_TIMEOUT;
        session.ready = session.ready && session.running;
        return session;
    }

    void Kh02CompanionProcess::wait_assets_ready(std::stop_token stopping) const
    {
        const auto health = impl->published_health.load();
        if (!health || !health->process || !health->assets_ready)
        {
            throw std::runtime_error("kh02_asset_readiness_session_missing");
        }
        health->assets_ready->wait(health->process, stopping, 90'000);
    }

    auto Kh02CompanionProcess::stop(const kh02control::Identity& expected) -> bool
    {
        std::lock_guard lock(impl->mutex);
        if (!impl->job || !impl->process || expected != impl->identity ||
            expected.owner_pid != GetCurrentProcessId())
        {
            return false;
        }
        impl->clear();
        return true;
    }

    void Kh02CompanionProcess::set_ready(const kh02control::Identity& identity)
    {
        std::lock_guard lock(impl->mutex);
        if (impl->identity != identity || !impl->session().running)
        {
            throw std::runtime_error("companion_readiness_session_changed");
        }
        impl->ready = true;
        impl->publish_health();
    }

    auto Kh02CompanionProcess::submit(kh02control::Request request)
        -> std::shared_ptr<kh02control::Ticket>
    {
        std::lock_guard lock(impl->mutex);
        if (!impl->control || request.identity != impl->identity)
        {
            throw std::runtime_error("companion_movie_session_not_ready");
        }
        request.generation = ++impl->request_generation;
        request.client_pid = GetCurrentProcessId();
        return impl->control->submit(request);
    }

} // namespace kh3ap
