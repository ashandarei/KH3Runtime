#pragma once
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include <string>
#include <variant>
#include <vector>

namespace kh3ap::kh02menu
{
    inline constexpr GfxProbeFunctionPaths gfx{L"/Script/ScaleformUI.GFxObject:GetMemberValue",
                                               L"/Script/ScaleformUI.GFxObject:Invoke",
                                               L"/Script/ScaleformUI.GFxObject:SetMemberValue"};
    using MovieArgument = std::variant<std::wstring, int, bool>;
    RC::Unreal::UObject* invoke_movie(RC::Unreal::UObject* object, const wchar_t* method,
                                      const std::vector<MovieArgument>& arguments = {}, double* number = nullptr);
    RC::Unreal::UObject* open_movie_asset(RC::Unreal::UObject* asset, int priority);
    RC::Unreal::UObject* find_movie_member(RC::Unreal::UObject* object, const wchar_t* path);
} // namespace kh3ap::kh02menu
