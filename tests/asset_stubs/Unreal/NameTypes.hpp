#pragma once
#include <set>
#include <string>
#include <string_view>
namespace RC::Unreal {
enum { FNAME_Add };
struct FName {
    const wchar_t* value{};
    FName()=default;
    FName(std::wstring_view text,int) {
        static std::set<std::wstring> names;
        value=names.emplace(text).first->c_str();
    }
};
static_assert(sizeof(FName)==8);
}
