#pragma once
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace kh3ap
{
    // Epic exchange codes are short lived. Ask the connected Linux client only
    // when a new owned companion is needed, and keep its reply entirely in IPC.
    inline auto kh02_store_launch_arguments(const nlohmann::json& configuration,
                                            std::stop_token cancellation) -> std::vector<std::wstring>
    {
        const auto wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
        if (!wine || configuration.value("source_platform", "") != "egs")
        {
            return {};
        }
        static std::atomic<uint64_t> serial{};
        const auto request_id = std::to_string(GetCurrentProcessId()) + ":" + std::to_string(GetTickCount64()) +
                                ":" + std::to_string(++serial);
        struct ClearLaunchChannels
        {
            ~ClearLaunchChannels()
            {
                pipe::erase(channels::runtime_kh02_launch_request);
                pipe::erase(channels::runtime_kh02_launch_response);
            }
        } cleanup;
        pipe::erase(channels::runtime_kh02_launch_response);
        const nlohmann::json request{{"request_id", request_id}, {"source_platform", "egs"},
                                     {"source_executable", configuration.at("source_executable")}};
        if (!pipe::write(channels::runtime_kh02_launch_request, request.dump()))
        {
            throw std::runtime_error("kh02_epic_launch_request_failed");
        }
        const auto encoded = pipe::wait_and_take(channels::runtime_kh02_launch_response, cancellation,
                                                std::chrono::seconds(55));
        if (!encoded)
        {
            throw std::runtime_error("kh02_epic_launch_requires_connected_client");
        }
        try
        {
            if (encoded->size() > 8192)
            {
                throw std::runtime_error("oversized reply");
            }
            const auto reply = nlohmann::json::parse(*encoded);
            if (reply.at("request_id") != request_id || reply.value("status", "") != "ready")
            {
                throw std::runtime_error("unavailable reply");
            }
            const auto arguments = reply.at("arguments").get<std::vector<std::string>>();
            if (arguments.empty() || arguments.size() > 16)
            {
                throw std::runtime_error("invalid arguments");
            }
            std::vector<std::wstring> result;
            for (const auto& argument : arguments)
            {
                const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argument.data(),
                                                       static_cast<int>(argument.size()), nullptr, 0);
                if (!count)
                {
                    throw std::runtime_error("invalid encoding");
                }
                std::wstring wide(count, L'\0');
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argument.data(), static_cast<int>(argument.size()),
                                   wide.data(), count);
                result.push_back(std::move(wide));
            }
            return result;
        }
        catch (...)
        {
            // JSON parser errors can embed input. Never forward an auth payload
            // into the provider error log or diagnostics.
            throw std::runtime_error("kh02_epic_launch_failed_sign_in_to_heroic");
        }
    }
}
