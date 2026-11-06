#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct RuntimeLoadingCompletion
    {
        bool complete{};
        bool prompt_ready{};
        bool dispatched{};
    };

    RuntimeLoadingCompletion kh02_finish_loading(RC::Unreal::UObject* pawn);
} // namespace kh3ap
