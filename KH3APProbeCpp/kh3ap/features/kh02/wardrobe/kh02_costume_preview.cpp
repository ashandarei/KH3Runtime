#include "kh02_preview_render_lease.h"
#include "kh02_preview_schedule.h"
#include "kh02_preview_reflection.h"
#include "kh02_costume_preview.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <array>
#include <numbers>
#include <chrono>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    using namespace kh02preview_detail;

    namespace
    {
        constexpr int material_load_uuid = 0x4b483490;
    }

    struct Kh02CostumePreview::Impl
    {
        Kh02PreviewRenderLease render_resources;
        Kh02PreviewSchedule schedule;
        FWeakObjectPtr material_request_pawn;
        uint64_t captures{};
        std::string error;

        void close(bool destroy_actor = true)
        {
            render_resources.close(destroy_actor);
            schedule.reset_capture();
        }

        void update(UObject* pawn, const std::vector<FWeakObjectPtr>& accessories, float angle)
        {
            if (!render_resources.ready_for(pawn))
            {
                close();
                constexpr auto material_path =
                    L"/Game/Blueprints/GoA/UI/Materials/M_KH02_WardrobePreview.M_KH02_WardrobePreview";
                auto* preview_material = find_loaded_runtime_asset(material_path);
                if (!preview_material)
                {
                    if (material_request_pawn.Get() != pawn)
                    {
                        Call instance(L"/Script/Engine.Default__GameplayStatics", L"GetGameInstance");
                        instance.object(L"WorldContextObject", pawn);
                        instance.run();
                        std::wstring load_error;
                        if (!request_runtime_asset_load(instance.result(), material_path, &load_error,
                                                        material_load_uuid))
                        {
                            throw std::runtime_error("Could not load Aqua preview material");
                        }
                        material_request_pawn = make_uobject_weak(pawn);
                        schedule.material_requested(std::chrono::steady_clock::now());
                    }
                    else if (schedule.material_timed_out(std::chrono::steady_clock::now()))
                    {
                        throw std::runtime_error("Aqua preview material missing; install the wardrobe GoA pak patch");
                    }
                    return;
                }
                render_resources.prepare(pawn, preview_material);
            }
            auto* capture = render_resources.capture_actor();
            const auto now = std::chrono::steady_clock::now();
            if (!schedule.begin_capture(now))
            {
                return;
            }
            auto* component = object_field(capture, L"SceneCaptureComponent2D");
            Call clear(component, L"ClearShowOnlyComponents");
            clear.object(L"InComponent", nullptr);
            clear.run();
            const auto show = [&](UObject* owner)
            {
                Call include(component, L"ShowOnlyActorComponents");
                include.object(L"inActor", owner);
                include.run();
            };
            show(pawn);
            auto* source_mesh = object_field(pawn, L"MyMesh");
            auto* preview_mesh = object_field(capture, L"MeshComponent");
            Call material_count(source_mesh, L"GetNumMaterials");
            material_count.run();
            const auto count =
                read_function_int_param(material_count.function, material_count.parameters.data(), L"ReturnValue")
                    .value_or(0);
            for (int index = 0; index < count && index < maximum_preview_material_slots; ++index)
            {
                Call get(source_mesh, L"GetMaterial");
                get.integer(L"ElementIndex", index);
                get.run();
                Call set(preview_mesh, L"SetMaterial");
                set.integer(L"ElementIndex", index);
                set.object(L"Material", get.result());
                set.run();
            }
            for (const auto& weak : accessories)
            {
                if (auto* accessory = weak.Get(); is_uobject_valid_fast(accessory))
                {
                    show(accessory);
                }
            }
            Call location(pawn, L"K2_GetActorLocation");
            location.run();
            const auto position = location.result_vector();
            Call rotation(pawn, L"K2_GetActorRotation");
            rotation.run();
            const auto facing = rotation.result_vector();
            const float yaw = facing[1] + angle;
            const float radians = yaw * std::numbers::pi_v<float> / half_turn_degrees;
            const auto& lights = render_resources.light_actors();
            for (size_t index = 0; index < lights.size(); ++index)
            {
                Call turn(lights[index].Get(), L"K2_SetActorRotation");
                const auto& lighting = preview_lights[index];
                turn.numbers(L"NewRotation", {lighting.pitch_degrees, yaw + lighting.yaw_offset_degrees, 0});
                turn.boolean(L"bTeleportPhysics", true);
                turn.run();
            }
            Call move_model(capture, L"K2_SetActorLocationAndRotation");
            move_model.vector(L"NewLocation", position[0], position[1], position[2]);
            move_model.numbers(L"NewRotation", {0, facing[1] - model_facing_offset_degrees, 0});
            move_model.boolean(L"bTeleport", true);
            move_model.run();
            Call move(component, L"K2_SetWorldLocationAndRotation");
            move.vector(L"NewLocation", position[0] + camera_distance_cm * std::cos(radians),
                        position[1] + camera_distance_cm * std::sin(radians), position[2] + camera_height_cm);
            move.numbers(L"NewRotation", {0, yaw + half_turn_degrees, 0});
            move.boolean(L"bSweep", false);
            move.boolean(L"bTeleport", true);
            move.run();
            Call render(component, L"CaptureScene");
            render.run();
            ++captures;
        }
    };

    Kh02CostumePreview::Kh02CostumePreview() : impl(std::make_unique<Impl>()) {}

    Kh02CostumePreview::~Kh02CostumePreview()
    {
        impl->close();
    }

    void Kh02CostumePreview::close(bool destroy_actor)
    {
        impl->close(destroy_actor);
        impl->error.clear();
        impl->material_request_pawn = {};
    }

    void Kh02CostumePreview::update(UObject* pawn, const std::vector<FWeakObjectPtr>& accessories, float angle)
    {
        if (!is_uobject_valid_fast(pawn) || !pawn->GetClassPrivate()->GetName().starts_with(L"p_ex002_Pawn"))
        {
            close();
            return;
        }
        if (!impl->error.empty())
        {
            return;
        }
        try
        {
            impl->update(pawn, accessories, angle);
        }
        catch (const std::exception& error)
        {
            impl->close();
            impl->error = error.what();
        }
    }

    auto Kh02CostumePreview::snapshot() const -> std::string
    {
        return nlohmann::json{{"active", is_uobject_valid_fast(impl->render_resources.widget())},
                              {"captures", impl->captures},
                              {"error", impl->error}}
            .dump();
    }
} // namespace kh3ap
