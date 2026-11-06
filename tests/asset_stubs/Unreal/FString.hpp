#pragma once
#include <cwchar>
#include <utility>
namespace RC::Unreal {
struct FString {
    wchar_t* data{}; int32_t count{}, capacity{};
    FString() = default;
    explicit FString(const wchar_t* text) {
        count=capacity=static_cast<int32_t>(std::wcslen(text)+1);
        data=new wchar_t[count]; std::wmemcpy(data,text,count);
    }
    FString(FString&& value) noexcept { *this=std::move(value); }
    auto operator=(FString&& value) noexcept -> FString& {
        delete[] data; data=std::exchange(value.data,nullptr);
        count=std::exchange(value.count,0); capacity=std::exchange(value.capacity,0); return *this;
    }
    ~FString() { delete[] data; }
};
}
