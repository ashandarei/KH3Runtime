#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr int max_player_control_parameter_bytes = 4096;

        int64_t integer(UObject* object, const wchar_t* name)
        {
            Kh02ReflectedCall call(object, name, "player_control",
                                   max_player_control_parameter_bytes);
            auto* property = call.param(L"ReturnValue");
            const auto kind = property->GetClass().GetName();
            if (kind != L"IntProperty" && kind != L"ByteProperty" && kind != L"EnumProperty")
            {
                throw std::runtime_error("invalid_integer_return");
            }
            const RuntimeGrantInternalCallDepth::Scope scope{};
            call.run();
            const auto value =
                read_function_int_param(call.function, call.parameters.data(), L"ReturnValue");
            if (!value)
            {
                throw std::runtime_error("unreadable_integer_return");
            }
            return *value;
        }

        bool boolean(UObject* object, const wchar_t* name)
        {
            Kh02ReflectedCall call(object, name, "player_control",
                                   max_player_control_parameter_bytes);
            const RuntimeGrantInternalCallDepth::Scope scope{};
            call.run();
            return call.boolean_result();
        }
    } // namespace

    bool runtime_sora_control_ready(UObject* pawn)
    {
        try
        {
            if (!is_uobject_valid_fast(pawn))
            {
                return false;
            }
            auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
            if (!is_uobject_valid_fast(controller) ||
                try_read_reflected_object_property_value(controller, L"Pawn") != pawn ||
                !boolean(controller, L"IsLocalController") ||
                boolean(controller, L"IsMoveInputIgnored") || integer(pawn, L"GetHitPoint") <= 0)
            {
                return false;
            }
            const auto state = integer(pawn, L"GetStateID");
            return state != player_state_ids::cinematic && state != player_state_ids::game_over &&
                   state != player_state_ids::die && state != player_state_ids::vanish_die;
        }
        catch (...)
        {
            return false;
        }
    }

    auto runtime_sora_game_over(UObject* pawn) -> bool
    {
        try
        {
            return is_uobject_valid_fast(pawn) &&
                   integer(pawn, L"GetStateID") == player_state_ids::game_over;
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace kh3ap
