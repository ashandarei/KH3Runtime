#include "kh02_provider_preparation.h"
#include "kh02_companion_process.h"
#include "kh02_remote_files.h"
#include "kh02_store_launch.h"
#include <nlohmann/json.hpp>
#define NOMINMAX
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <thread>

namespace kh3ap
{
    namespace
    {
        constexpr uintmax_t maximum_configuration_bytes = 4096;

        auto configuration(const wchar_t* filename) -> nlohmann::json
        {
            wchar_t executable[32768]{};
            const auto length =
                GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
            if (!length || length == std::size(executable))
            {
                throw std::runtime_error("kh02_provider_executable_path");
            }
            const auto path = std::filesystem::path(executable).parent_path() / filename;
            if (!std::filesystem::exists(path))
            {
                return nlohmann::json::object();
            }
            if (std::filesystem::file_size(path) > maximum_configuration_bytes)
            {
                throw std::runtime_error("kh02_provider_config_size");
            }
            std::ifstream stream(path);
            return nlohmann::json::parse(stream);
        }

        auto start_provider(Kh02CompanionProcess& process,
                            const std::shared_ptr<std::atomic<bool>>& stopping,
                            std::stop_token cancellation) -> kh02control::Identity
        {
            const auto config = configuration(L"kh02_portal_config.json");
            const auto source = config.at("source_executable").get<std::string>();
            const auto path = std::filesystem::path(std::u8string(source.begin(), source.end()));
            const auto arguments = kh02_store_launch_arguments(config, cancellation);
            if (cancellation.stop_requested())
            {
                throw std::runtime_error("kh02_provider_cancelled");
            }
            const auto session = process.start(path.wstring(), arguments);
            if (!session.running || !session.owned || !session.executable_verified ||
                !session.input_isolated)
            {
                throw std::runtime_error("kh02_provider_source_not_owned");
            }
            kh02control::Request request;
            request.identity = session.identity;
            request.client_pid = GetCurrentProcessId();
            request.generation = 1;
            request.operation = kh02control::Operation::inspect_assets;
            process.wait_assets_ready(cancellation);
            const auto reply = kh02control::exchange(request, stopping.get());
            if (reply.status == kh02control::Status::accepted)
            {
                prepare_kh02_asset_index(session.identity.source_pid);
                if (cancellation.stop_requested())
                {
                    throw std::runtime_error("kh02_provider_cancelled");
                }
                process.set_ready(session.identity);
                return session.identity;
            }
            if (reply.status == kh02control::Status::wrong_session)
            {
                throw std::runtime_error("kh02_provider_session_or_catalog_mismatch");
            }
            throw std::runtime_error("kh02_provider_readiness_confirmation_failed");
        }
    } // namespace

    Kh02ProviderPreparation::Kh02ProviderPreparation(Kh02CompanionProcess& source) : process(source)
    {
    }

    Kh02ProviderPreparation::~Kh02ProviderPreparation()
    {
        stopping->store(true, std::memory_order_relaxed);
        pending.request_stop();
        if (pending.joinable())
        {
            pending.join();
        }
    }

    auto Kh02ProviderPreparation::enabled() -> bool
    {
        return configuration(L"kh02_story_config.json").value("prepare_gameplay_on_entry", false);
    }

    auto Kh02ProviderPreparation::prepare() -> bool
    {
        const auto current = process.health();
        if (binding_failed)
        {
            throw std::runtime_error("kh02_provider_restart_required");
        }
        if (imported_identity.source_pid &&
            (!current.ready || current.identity != imported_identity))
        {
            throw std::runtime_error("kh02_provider_restart_required");
        }
        if (bound.source_pid)
        {
            if (!current.ready || current.identity != bound)
            {
                throw std::runtime_error("kh02_provider_restart_required");
            }
            return true;
        }
        if (!pending.joinable())
        {
            completed.store(false, std::memory_order_relaxed);
            preparation_error = nullptr;
            pending = std::jthread(
                [this, cancellation = stopping](std::stop_token token)
                {
                    try
                    {
                        prepared_identity = start_provider(process, cancellation, token);
                    }
                    catch (...)
                    {
                        preparation_error = std::current_exception();
                    }
                    completed.store(true, std::memory_order_release);
                });
            return false;
        }
        if (!completed.load(std::memory_order_acquire))
        {
            return false;
        }
        pending.join();
        completed.store(false, std::memory_order_relaxed);
        if (preparation_error)
        {
            std::rethrow_exception(preparation_error);
        }
        const auto identity = prepared_identity;
        const auto ready = process.health();
        if (!ready.ready || ready.identity != identity)
        {
            throw std::runtime_error("kh02_provider_binding_session_changed");
        }
        bound = identity;
        try
        {
            bind_kh02_remote_files(identity.source_pid);
        }
        catch (const std::exception&)
        {
            // Native adapter installation is irreversible within this process.
            // A failed bind may already have installed it; never rebind blindly.
            binding_failed = true;
            throw std::runtime_error("kh02_provider_restart_required");
        }
        return true;
    }

    void Kh02ProviderPreparation::reset()
    {
        if (bound.source_pid)
        {
            const auto current = process.health();
            if (binding_failed || !current.ready || current.identity != bound)
            {
                throw std::runtime_error("kh02_provider_restart_required");
            }
            // The installed native adapter cannot change provider PID, even if
            // no package has yet requested an imported asset.
            return;
        }
        stopping->store(true, std::memory_order_relaxed);
        pending.request_stop();
        if (pending.joinable())
        {
            pending.join();
        }
        completed.store(false, std::memory_order_relaxed);
        preparation_error = nullptr;
        prepared_identity = {};
        bound = {};
        stopping = std::make_shared<std::atomic<bool>>(false);
    }

    auto Kh02ProviderPreparation::completion_ready() const -> bool
    {
        return completed.load(std::memory_order_acquire);
    }

    void Kh02ProviderPreparation::mark_content_requested()
    {
        const auto current = process.health();
        if (!current.ready || !current.owned || current.identity != bound)
        {
            throw std::runtime_error("kh02_provider_import_requires_bound_session");
        }
        imported_identity = current.identity;
    }

} // namespace kh3ap
