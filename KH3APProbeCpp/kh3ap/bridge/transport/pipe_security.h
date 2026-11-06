#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <sddl.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace kh3ap::pipe
{
    // Protect gameplay IPC from other local users as well as remote clients.
    // Construction fails rather than falling back to a permissive default DACL.
    class UserPipeSecurity
    {
    public:
        UserPipeSecurity()
        {
            HANDLE raw_token{};
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token))
            {
                throw std::runtime_error("pipe user token unavailable");
            }
            const std::unique_ptr<void, CloseToken> token{raw_token};
            DWORD bytes{};
            GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
            if (!bytes || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            {
                throw std::runtime_error("pipe user identity size unavailable");
            }
            std::vector<unsigned char> data(bytes);
            if (!GetTokenInformation(token.get(), TokenUser, data.data(), bytes, &bytes))
            {
                throw std::runtime_error("pipe user identity unavailable");
            }
            LPWSTR raw_sid{};
            if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,
                                        &raw_sid))
            {
                throw std::runtime_error("pipe user SID unavailable");
            }
            const std::unique_ptr<wchar_t, FreeLocal> sid{raw_sid};
            const auto dacl = std::wstring{L"D:P(A;;GA;;;SY)(A;;GA;;;"} + sid.get() + L")";
            PSECURITY_DESCRIPTOR raw_descriptor{};
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(dacl.c_str(), SDDL_REVISION_1,
                                                                      &raw_descriptor, nullptr))
            {
                throw std::runtime_error("pipe security descriptor unavailable");
            }
            descriptor_.reset(raw_descriptor);
            attributes_.lpSecurityDescriptor = descriptor_.get();
        }

        UserPipeSecurity(const UserPipeSecurity&) = delete;
        auto operator=(const UserPipeSecurity&) -> UserPipeSecurity& = delete;

        auto attributes() -> SECURITY_ATTRIBUTES*
        {
            return &attributes_;
        }

    private:
        struct CloseToken
        {
            auto operator()(void* token) const noexcept -> void
            {
                CloseHandle(token);
            }
        };
        struct FreeLocal
        {
            auto operator()(void* memory) const noexcept -> void
            {
                LocalFree(memory);
            }
        };
        std::unique_ptr<void, FreeLocal> descriptor_;
        SECURITY_ATTRIBUTES attributes_{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    };
} // namespace kh3ap::pipe
