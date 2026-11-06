#include "kh02_story_flag_bindings.h"
#include "kh3ap/features/kh02/progress/kh02_progress_codec.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/FProperty.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <cstring>
#include <cwctype>
#include <stdexcept>
#include <DynamicOutput/DynamicOutput.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    struct Kh02StoryFlagBindings::Impl
    {
        static constexpr int source_name_operand_bytes = 13;
        static constexpr int script_name_bytes = 12;
        static constexpr int native_name_bytes = 8;
        static constexpr uint8_t name_constant_opcode = 0x21;
        static constexpr uint8_t true_constant_opcode = 0x27;
        static constexpr uint8_t false_constant_opcode = 0x28;
        static constexpr uint8_t end_parameters_opcode = 0x16;
        static constexpr uint8_t return_opcode = 0x04;
        static constexpr uint8_t nothing_opcode = 0x0b;
        static constexpr uint8_t end_script_opcode = 0x53;
        using Thunk = void (*)(UObject*, FFrame&, void*);

        struct Binding
        {
            FWeakObjectPtr fn;
            Thunk original{}, replacement{};
        };

        std::array<Binding, 7> bindings{};
        Callbacks callbacks;
        uint64_t reads{}, writes{}, rejected{}, forwarded{};
        std::wstring last_node, last_object, last_key, error;
        static inline Impl* instance{};

        explicit Impl(Callbacks observers) : callbacks(std::move(observers)) {}

        ~Impl()
        {
            for (auto& b : bindings)
            {
                if (auto* f = static_cast<UFunction*>(b.fn.Get());
                    is_uobject_valid_fast(f) && f->GetFuncPtr() == b.replacement)
                {
                    f->SetFuncPtr(b.original);
                }
            }
            if (instance == this)
            {
                instance = nullptr;
            }
        }

        bool source(FFrame_50_AndBelow* f)
        {
            if (!f->Code || !is_uobject_valid_fast(f->Node))
            {
                return false;
            }
            if (!f->Node->GetPathName().starts_with(L"/Game/Z"))
            {
                return false;
            }
            // EX_Context can make Object the native library CDO. Node and Code
            // still identify the caller graph and its operands. Never forward a
            // source graph merely because its current context is that CDO.
            last_node = f->Node->GetPathName();
            last_object = is_uobject_valid_fast(f->Object) ? f->Object->GetPathName() : L"<invalid>";
            return true;
        }

        template <int I> static void thunk(UObject* o, FFrame& stack, void* result)
        {
            auto* self = instance;
            auto* f = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (!self->source(f))
            {
                ++self->forwarded;
                self->bindings[I].original(o, stack, result);
                return;
            }
            // These catalog-audited calls use literal names/booleans. Consume the
            // same VM operands and P_FINISH as the owning native thunk. No native
            // flag getter/setter is called on the source path, even on rejection.
            auto& script = f->Node->GetScript();
            auto* begin = script.GetData();
            auto* end = begin + script.Num();
            auto* p = f->Code;
            auto name = [&]() -> std::wstring
            {
                if (p < begin || end - p < source_name_operand_bytes || *p++ != name_constant_opcode)
                {
                    throw std::runtime_error("source_flag_name_operand");
                }
                // FScriptName is always ComparisonIndex/DisplayIndex/Number (12),
                // including this non-case-preserving game whose FName is 8 bytes.
                uint32_t script_name[3];
                std::memcpy(script_name, p, script_name_bytes);
                p += script_name_bytes;
                const uint32_t native_name[]{script_name[0], script_name[2]};
                FName value;
                static_assert(sizeof(value) == native_name_bytes);
                std::memcpy(&value, native_name, native_name_bytes);
                return value.ToString();
            };
            try
            {
                self->callbacks.ensure_world(f->Object);
                const auto key = name();
                self->last_key = key;
                auto normalized = key;
                for (auto& c : normalized)
                {
                    c = std::towlower(c);
                }
                // Share the persistence allowlist. Source DW06 names its seven
                // collectible flags _01.._07, including the leading zero.
                if (!kh02_progress::flag_key(to_utf8(normalized)))
                {
                    throw std::runtime_error("source_flag_outside_dw");
                }
                std::wstring label;
                int boolean = 0;
                if constexpr (I == 2 || I >= 4)
                {
                    label = name();
                }
                if constexpr (I == 3)
                {
                    if (p >= end || (*p != true_constant_opcode && *p != false_constant_opcode))
                    {
                        throw std::runtime_error("source_flag_bool_operand");
                    }
                    boolean = *p++ == true_constant_opcode;
                }
                if (p >= end || *p++ != end_parameters_opcode)
                {
                    throw std::runtime_error("source_flag_end_operand");
                }
                f->Code = p;
                const Request request{
                    static_cast<Operation>(I), key, normalized, label, boolean, f->Node->GetPathName(),
                    result != nullptr};
                const int value = self->callbacks.evaluate(request);
                if (result)
                {
                    if constexpr (I == 0)
                    {
                        *static_cast<int32_t*>(result) = value;
                    }
                    else if constexpr (I != 2 && I != 3)
                    {
                        *static_cast<uint8_t*>(result) = value != 0;
                    }
                }
            }
            catch (const std::exception& e)
            {
                ++self->rejected;
                const std::string message = e.what();
                self->error.assign(message.begin(), message.end());
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] KH02 private flag rejected error={} key={} node={} opcode={} remaining={}\n"),
                    self->error, self->last_key, self->last_node, unsigned(*f->Code), end - f->Code);
                // The audited ubergraphs end Return/Nothing/EndOfScript. Return
                // exits a rejected graph; executing EndOfScript itself is fatal.
                if (end - begin >= 3 && end[-3] == return_opcode && end[-2] == nothing_opcode &&
                    end[-1] == end_script_opcode)
                {
                    f->Code = end - 3;
                }
                if (result)
                {
                    if constexpr (I == 0)
                    {
                        *static_cast<int32_t*>(result) = 0;
                    }
                    else if constexpr (I != 2 && I != 3)
                    {
                        *static_cast<uint8_t*>(result) = 0;
                    }
                }
            }
        }

        void install()
        {
            const wchar_t* names[]{L"GetGameFlag",        L"GetGameFlagBoolean",   L"SetFlagLabel",
                                   L"SetGameFlagBoolean", L"EqualEqual_FlagLabel", L"GreaterEqual_FlagLabel",
                                   L"Less_FlagLabel"};
            Thunk replacements[]{thunk<0>, thunk<1>, thunk<2>, thunk<3>, thunk<4>, thunk<5>, thunk<6>};
            if (instance && instance != this)
            {
                throw std::runtime_error("source_flag_instance");
            }
            instance = this;
            for (int i = 0; i < 7; ++i)
            {
                auto& b = bindings[i];
                if (b.original)
                {
                    continue;
                }
                const auto path = std::wstring(L"/Script/TresGame.TresGameBlueprintLibrary:") + names[i];
                auto* fn =
                    UObjectGlobals::StaticFindObject<UFunction*>(UFunction::StaticClass(), nullptr, path.c_str());
                if (!is_uobject_valid_fast(fn) || !(fn->GetFunctionFlags() & FUNC_Native) || !fn->GetFuncPtr())
                {
                    throw std::runtime_error("source_flag_function");
                }
                const auto expected = i == 0 ? 12 : i == 1 ? 9 : i == 2 ? 16 : i == 3 ? 9 : 17;
                int fields = 0;
                for (auto* p : fn->ForEachProperty())
                {
                    ++fields;
                    const auto offset = p->GetOffset_Internal();
                    const auto size = p->GetElementSize();
                    const auto kind = p->GetClass().GetName();
                    const bool name = kind == L"NameProperty" && size == 8 &&
                                      (offset == 0 || offset == 8 && i != 0 && i != 1 && i != 3);
                    const bool integer = i == 0 && offset == 8 && size == 4 && kind == L"IntProperty";
                    const bool boolean = kind == L"BoolProperty" && size == 1 &&
                                         ((i == 1 || i == 3) && offset == 8 || i >= 4 && offset == 16);
                    if (!name && !integer && !boolean)
                    {
                        throw std::runtime_error("source_flag_parameter_layout");
                    }
                }
                if (fn->GetParmsSize() != expected || fields != (i >= 4 ? 3 : 2))
                {
                    throw std::runtime_error("source_flag_signature");
                }
                b.fn = make_uobject_weak(fn);
                b.original = fn->GetFuncPtr();
                b.replacement = replacements[i];
                fn->SetFuncPtr(b.replacement);
            }
        }
    };

    Kh02StoryFlagBindings::Kh02StoryFlagBindings(Callbacks callbacks)
        : impl(std::make_unique<Impl>(std::move(callbacks)))
    {
    }

    Kh02StoryFlagBindings::~Kh02StoryFlagBindings() = default;

    void Kh02StoryFlagBindings::install()
    {
        impl->install();
    }

    void Kh02StoryFlagBindings::note_read()
    {
        ++impl->reads;
    }

    void Kh02StoryFlagBindings::note_write()
    {
        ++impl->writes;
    }

    void Kh02StoryFlagBindings::reset_observations()
    {
        impl->reads = 0;
        impl->writes = 0;
        impl->rejected = 0;
        impl->error.clear();
    }

    auto Kh02StoryFlagBindings::snapshot() const -> nlohmann::json
    {
        return {{"reads", impl->reads},
                {"writes", impl->writes},
                {"rejected", impl->rejected},
                {"forwarded", impl->forwarded},
                {"last_key", to_utf8(impl->last_key)},
                {"last_source_node", to_utf8(impl->last_node)},
                {"last_source_object", to_utf8(impl->last_object)},
                {"error", to_utf8(impl->error)}};
    }
} // namespace kh3ap
