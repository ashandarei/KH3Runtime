#include "kh02_information_queue.h"
#include "kh3ap/features/kh02/story/kh02_map_session.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr size_t pending_notice_limit = 16;
        constexpr size_t notice_history_limit = 64;
        constexpr size_t parameter_limit_bytes = 2'048;
        constexpr uint64_t notice_spacing_ms = 6'500;
        constexpr float notice_display_seconds = 6.0f;
    } // namespace

    struct Kh02InformationQueue::Impl
    {
        struct SourceInformation
        {
            FWeakObjectPtr world, schema;
            Kh02MapSession::Generation generation{};
            std::vector<std::byte> text;
            std::wstring caller;
            uint64_t queued_at{};
            bool auto_close{};

            ~SourceInformation()
            {
                if (auto* type = static_cast<UScriptStruct*>(schema.Get()); is_uobject_valid_fast(type))
                {
                    type->DestroyStruct(text.data());
                }
            }
        };

        std::vector<std::unique_ptr<SourceInformation>> pending_information;
        uint64_t information_next_at{};
        Json information_events = Json::array();

        void tick(UObject* world, const Kh02MapSession& session, bool can_show)
        {
            std::erase_if(pending_information, [&session, world](const auto& notice)
                          { return notice->world.Get() != world || !session.accepts(notice->generation); });
            if (pending_information.empty() || !can_show || GetTickCount64() < information_next_at)
            {
                return;
            }
            auto notice = std::move(pending_information.front());
            pending_information.erase(pending_information.begin());
            Kh02ReflectedCall show(L"/Script/TresGame.Default__TresGameBlueprintLibrary", L"TresUI_OpenInformation",
                                   "kh02_story", parameter_limit_bytes);
            auto* text = show.param(L"InformationText");
            auto* schema = static_cast<UScriptStruct*>(notice->schema.Get());
            if (!text || !text->IsA<FStructProperty>() || !is_uobject_valid_fast(schema) ||
                static_cast<FStructProperty*>(text)->GetStruct() != schema ||
                text->GetElementSize() != notice->text.size())
            {
                throw std::runtime_error("kh02_information_text_layout");
            }
            std::memcpy(text->ContainerPtrToValuePtr<void>(show.parameters.data()), notice->text.data(),
                        notice->text.size());
            show.boolean(L"AutoClose", notice->auto_close);
            show.number(L"OptionalDisplayTime", notice_display_seconds);
            show.run();
            const auto now = GetTickCount64();
            information_next_at = now + notice_spacing_ms;
            if (information_events.size() < notice_history_limit)
            {
                information_events.push_back({{"caller", to_utf8(notice->caller)},
                                              {"queued_at_ms", notice->queued_at},
                                              {"shown_at_ms", now},
                                              {"display_seconds", notice_display_seconds}});
            }
        }
    };

    Kh02InformationQueue::Kh02InformationQueue() : impl(std::make_unique<Impl>()) {}

    Kh02InformationQueue::~Kh02InformationQueue() = default;

    auto Kh02InformationQueue::full() const -> bool
    {
        return impl->pending_information.size() >= pending_notice_limit;
    }

    auto Kh02InformationQueue::pending() const -> size_t
    {
        return impl->pending_information.size();
    }

    auto Kh02InformationQueue::events() const -> const Json&
    {
        return impl->information_events;
    }

    void Kh02InformationQueue::clear()
    {
        impl->pending_information.clear();
    }

    void Kh02InformationQueue::tick(UObject* world, const Kh02MapSession& session, bool can_show)
    {
        impl->tick(world, session, can_show);
    }

    void Kh02InformationQueue::enqueue(UObject* world, uint64_t generation, UScriptStruct* schema, const void* text,
                                       std::wstring caller, bool auto_close)
    {
        if (full() || !is_uobject_valid_fast(world) || !is_uobject_valid_fast(schema) || !text)
        {
            throw std::runtime_error("kh02_information_queue_invalid_notice");
        }
        auto notice = std::make_unique<Impl::SourceInformation>();
        notice->world = make_uobject_weak(world);
        notice->generation = generation;
        notice->caller = std::move(caller);
        notice->queued_at = GetTickCount64();
        notice->auto_close = auto_close;
        notice->text.resize(schema->GetPropertiesSize());
        schema->InitializeStruct(notice->text.data());
        notice->schema = make_uobject_weak(schema);
        schema->CopyScriptStruct(notice->text.data(), text);
        impl->pending_information.push_back(std::move(notice));
    }
} // namespace kh3ap
