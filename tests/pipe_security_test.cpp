#include "kh3ap/bridge/transport/pipe_security.h"

#include <cassert>
#include <type_traits>

int main()
{
    using kh3ap::pipe::UserPipeSecurity;
    static_assert(!std::is_copy_constructible_v<UserPipeSecurity>);
    static_assert(!std::is_move_constructible_v<UserPipeSecurity>);
    UserPipeSecurity security;
    const auto* attributes = security.attributes();
    assert(attributes->nLength == sizeof(SECURITY_ATTRIBUTES));
    assert(!attributes->bInheritHandle);
    const auto descriptor = attributes->lpSecurityDescriptor;
    assert(IsValidSecurityDescriptor(descriptor));
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision{};
    assert(GetSecurityDescriptorControl(descriptor, &control, &revision));
    assert(control & SE_DACL_PROTECTED);
    PACL dacl{};
    BOOL present{}, defaulted{};
    assert(GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted));
    assert(present && dacl && !defaulted && dacl->AceCount == 2);
    HANDLE token{};
    assert(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    DWORD bytes{};
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<unsigned char> user(bytes);
    assert(GetTokenInformation(token, TokenUser, user.data(), bytes, &bytes));
    CloseHandle(token);
    const auto user_sid = reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;
    bool allows_user{}, allows_system{};
    for (DWORD index = 0; index < dacl->AceCount; ++index)
    {
        void* raw_ace{};
        assert(GetAce(dacl, index, &raw_ace));
        const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw_ace);
        assert(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE);
        assert(ace->Mask == GENERIC_ALL);
        auto* sid = const_cast<DWORD*>(&ace->SidStart);
        const bool current_user = EqualSid(sid, user_sid) != FALSE;
        const bool system = IsWellKnownSid(sid, WinLocalSystemSid) != FALSE;
        assert(current_user || system);
        allows_user |= current_user;
        allows_system |= system;
    }
    assert(allows_user && allows_system);
}
