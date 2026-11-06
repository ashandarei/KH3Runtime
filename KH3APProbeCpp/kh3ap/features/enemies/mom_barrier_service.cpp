#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/enemies/mom_barrier_service.h"
#include "kh3ap/features/treasure/music_chest/music_chest_service.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/messages/ap_message_param_storage.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include <Unreal/AActor.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FMulticastDelegateProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/World.hpp>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
namespace kh3ap {
    namespace
    {
        constexpr auto barrier_interaction_cooldown = std::chrono::seconds(8);
        constexpr auto barrier_request_update_interval = std::chrono::milliseconds(250);
        constexpr auto max_barrier_call_parameter_bytes = 4096;
        constexpr auto barrier_interaction_radius = 600;
        constexpr auto max_barrier_preview_coordinate = 10000000;
        constexpr auto max_barrier_preview_yaw_degrees = 360;
        constexpr auto max_barrier_preview_pitch_degrees = 90;
    }

using namespace RC::Unreal;
namespace {
constexpr auto asset=L"/Game/Mods/KH3APDiagnostics/MoMBarrier/A_MoMBarrier.A_MoMBarrier_C";
constexpr auto chest_asset=L"/Game/Mods/KH3APDiagnostics/MoMBarrier/A_MoMChest.A_MoMChest_C";
constexpr auto native_chest_asset=L"/Game/Mods/KH3APDiagnostics/MoMBarrier/A_MoMNativeChest.A_MoMNativeChest_C";
constexpr auto native_mesh=L"/Game/Maps/ra/gimmick/g_ra_TreasureBox_l/mdl/g_ra_TreasureBox_l.g_ra_TreasureBox_l";
constexpr auto native_diffuse=L"/Game/Maps/ra/gimmick/g_ra_TreasureBox_l/mdl/T_ra_TreasureBox_l_D.T_ra_TreasureBox_l_D";
constexpr auto native_open=L"/Game/Character/gimmick/g_dw_TreasureBox_l/anm/rt_dw/body/002.002";
using Vec=std::array<float,3>;
using Clock=std::chrono::steady_clock;
int64_t unix_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
struct Call {
    UObject* obj; UFunction* fn;std::vector<std::byte> data;
    Call(UObject* o,const wchar_t* name):obj(o) {
        if(!is_uobject_valid_fast(o))throw std::runtime_error("invalid_mom_call_object");
        fn=o->GetFunctionByNameInChain(name);
        if(!is_uobject_valid_fast(fn)||fn->GetParmsSize()>max_barrier_call_parameter_bytes)throw std::runtime_error("missing_mom_function");
        data.resize(std::max<int>(1,fn->GetParmsSize()));
    }
    FProperty* field(const wchar_t* name) {
        auto* p=find_function_param_by_name(fn,name);
        if(!p||p->GetOffset_Internal()<0||p->GetOffset_Internal()+p->GetElementSize()>fn->GetParmsSize())throw std::runtime_error("mom_parameter_layout");
        return p;
    }
    void run() { obj->ProcessEvent(fn,data.data()); }
    void boolean(const wchar_t* name,bool v) { if(!write_bool_function_param(field(name),data.data(),v))throw std::runtime_error("mom_bool_layout"); }
};
Vec position(UObject* o) {
    Call c(o,L"K2_GetActorLocation");auto* p=c.field(L"ReturnValue");
    if(!p->IsA<FStructProperty>()||p->GetElementSize()!=12)throw std::runtime_error("mom_vector_layout");
    c.run();Vec v;std::memcpy(v.data(),p->ContainerPtrToValuePtr<void>(c.data.data()),12);return v;
}
void vector(Call& c,const wchar_t* name,const Vec& v) {
    auto* p=c.field(name);if(!p->IsA<FStructProperty>()||p->GetElementSize()!=12)throw std::runtime_error("mom_vector_layout");
    std::memcpy(p->ContainerPtrToValuePtr<void>(c.data.data()),v.data(),12);
}
UObject* world(UObject* a) { return is_uobject_valid_fast(a)?static_cast<AActor*>(a)->GetWorld():nullptr; }
const wchar_t* visual_asset(bool chest,bool native=false) {return native?native_chest_asset:chest?chest_asset:asset;}
UClass* klass(bool chest=false,bool native=false) { auto* c=UObjectGlobals::StaticFindObject<UClass*>(nullptr,nullptr,visual_asset(chest,native));return is_uobject_valid_fast(c)?c:nullptr; }
UObject* loaded(const wchar_t* path) {auto* o=UObjectGlobals::StaticFindObject<UObject*>(nullptr,nullptr,path);return is_uobject_valid_fast(o)?o:nullptr;}
std::string vec_json(const Vec& v) { std::ostringstream s;s<<'['<<v[0]<<','<<v[1]<<','<<v[2]<<']';return s.str(); }
bool native_bool(UObject* o,const wchar_t* name) {
    Call c(o,name);auto* p=c.field(L"ReturnValue");if(!p->IsA<FBoolProperty>())throw std::runtime_error("mom_bool_result_layout");
    c.run();return static_cast<FBoolProperty*>(p)->GetPropertyValueInContainer(c.data.data());
}
float chest_ground(UObject* pawn,const Vec& origin) {
    auto* library=UObjectGlobals::StaticFindObject<UObject*>(nullptr,nullptr,L"/Script/Engine.Default__KismetSystemLibrary");
    Call trace(library,L"LineTraceSingle");
    if(!write_object_function_param(trace.fn,trace.data.data(),L"WorldContextObject",pawn))throw std::runtime_error("mom_trace_context");
    vector(trace,L"Start",{origin[0]+350,origin[1],origin[2]+150});
    vector(trace,L"End",{origin[0]+350,origin[1],origin[2]-1200});
    trace.boolean(L"bTraceComplex",true);trace.boolean(L"bIgnoreSelf",true);trace.run();
    auto* result=trace.field(L"ReturnValue");
    if(!result->IsA<FBoolProperty>()||!static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(trace.data.data()))
        throw std::runtime_error("mom_chest_ground_not_found");
    auto* hit=trace.field(L"OutHit");
    if(!hit->IsA<FStructProperty>())throw std::runtime_error("mom_chest_ground_layout");
    auto* point=find_struct_field_by_name(static_cast<FStructProperty*>(hit)->GetStruct(),L"ImpactPoint");
    if(!point||!point->IsA<FStructProperty>()||point->GetElementSize()!=12)throw std::runtime_error("mom_chest_ground_point");
    Vec location{};std::memcpy(location.data(),point->ContainerPtrToValuePtr<void>(hit->ContainerPtrToValuePtr<void>(trace.data.data())),12);
    return location[2]+1.f;
}
}
struct RuntimeMoMBarrierService::Impl {
    RuntimeEnemyClearConfig config;
    FWeakObjectPtr actor,reactor,owner_world;
    Vec origin{};
    std::wstring id,seed,song_name;
    int64_t slot{},location_id{},interactions{};
    int difficulty{};
    int callback_count{};
    bool active{},cleared{};
    bool chest{},opening_pending{},opening{},opened{};
    bool native{},preview{};
    Vec preview_position{};
    float preview_yaw{};
    float animation_position{},animation_length{};
    float lid_pitch{};
    Clock::time_point opening_start{};
    Clock::time_point next{},cooldown{};
    std::string error,sweep{"null"};
    RuntimeApMessageParamStorage message_storage;
    inline static Impl* instance{};
    explicit Impl(RuntimeEnemyClearConfig c):config(std::move(c)) { instance=this; }
    ~Impl() { if(instance==this)instance=nullptr; }
    UObject* component(const wchar_t* name) const {
        auto* a=actor.Get();
        auto* c=is_uobject_valid_fast(a)?try_read_reflected_object_property_value(a,name):nullptr;
        if(!is_uobject_valid_fast(c))throw std::runtime_error("mom_owned_component_missing");
        // UE4SS copies existing weak serials rather than allocating one. Plain
        // scene components can have serial zero; resolve them through the weak
        // owner each time instead of retaining an unusable weak component.
        return c;
    }
    void pose_native(float fraction) {
        auto* mesh=component(L"NativeChestMesh");auto* anim=loaded(native_open);
        if(!anim)throw std::runtime_error("mom_native_animation_missing");
        auto* length=anim->GetPropertyByNameInChain(L"SequenceLength");
        if(!length||length->GetElementSize()!=4)throw std::runtime_error("mom_native_animation_length");
        std::memcpy(&animation_length,length->ContainerPtrToValuePtr<void>(anim),4);
        if(!std::isfinite(animation_length)||animation_length<=0)throw std::runtime_error("mom_native_animation_duration");
        Call pose(mesh,L"OverrideAnimationData");
        if(!write_object_function_param(pose.fn,pose.data.data(),L"InAnimToPlay",anim)||
            !write_float_function_param(pose.field(L"Position"),pose.data.data(),animation_length*fraction)||
            !write_float_function_param(pose.field(L"PlayRate"),pose.data.data(),0))throw std::runtime_error("mom_native_pose_layout");
        pose.boolean(L"bIsLooping",false);pose.boolean(L"bIsPlaying",false);pose.run();
        // OverrideAnimationData initializes a new single-node instance, but
        // does not seek an instance already in that mode. Seek it explicitly,
        // then evaluate its bones synchronously without animation notifies.
        Call seek(mesh,L"SetPosition");
        if(!write_float_function_param(seek.field(L"InPos"),seek.data.data(),animation_length*fraction))throw std::runtime_error("mom_native_seek_layout");
        seek.boolean(L"bFireNotifies",false);seek.run();pose.run();
        Call position(mesh,L"GetPosition");position.run();auto* result=position.field(L"ReturnValue");
        if(result->GetElementSize()!=4)throw std::runtime_error("mom_native_position_layout");
        std::memcpy(&animation_position,result->ContainerPtrToValuePtr<void>(position.data.data()),4);
        if(std::abs(animation_position-animation_length*fraction)>.02f)throw std::runtime_error("mom_native_pose_readback");
    }
    void setup_native() {
        auto* mesh=component(L"NativeChestMesh");auto* model=loaded(native_mesh);auto* diffuse=loaded(native_diffuse);
        if(!model||!diffuse)throw std::runtime_error("mom_native_assets_missing");
        Call set(mesh,L"SetSkeletalMesh");
        if(!write_object_function_param(set.fn,set.data.data(),L"NewMesh",model))throw std::runtime_error("mom_native_mesh_layout");
        set.boolean(L"bReinitPose",true);set.run();
        if(try_read_reflected_object_property_value(mesh,L"SkeletalMesh")!=model)throw std::runtime_error("mom_native_mesh_readback");
        Call material(mesh,L"CreateDynamicMaterialInstance");material.run();
        auto* dynamic=try_read_function_object_param(material.fn,material.data.data(),L"ReturnValue");
        Call texture(dynamic,L"SetTextureParameterValue");
        if(!write_name_or_string_function_param(texture.field(L"ParameterName"),texture.data.data(),L"NativeDiffuse")||
           !write_object_function_param(texture.fn,texture.data.data(),L"Value",diffuse))throw std::runtime_error("mom_native_texture_layout");
        texture.run();
        Call read_texture(dynamic,L"K2_GetTextureParameterValue");
        if(!write_name_or_string_function_param(read_texture.field(L"ParameterName"),read_texture.data.data(),L"NativeDiffuse"))throw std::runtime_error("mom_native_texture_read_layout");
        read_texture.run();
        if(try_read_function_object_param(read_texture.fn,read_texture.data.data(),L"ReturnValue")!=diffuse)throw std::runtime_error("mom_native_texture_readback");
        pose_native(0);
    }
    std::string identity() const {
        std::ostringstream s;s<<"\"schema\":\"kh3ap.mom_barrier.v1\",\"barrier_id\":\""<<json_escape(id)
            <<"\",\"process_id\":"<<GetCurrentProcessId()<<",\"seed_name\":\""<<json_escape(seed)
            <<"\",\"slot\":"<<slot<<",\"ap_location_id\":"<<location_id;return s.str();
    }
    std::string snapshot() {
        // These fixed preview assets are reused across worlds. Retain each as
        // soon as it arrives, before another load/travel can collect it.
        if(native)for(auto path:{native_chest_asset,native_mesh,native_diffuse,native_open})
            if(auto* asset=loaded(path))asset->SetRootSet();
        auto* a=actor.Get();auto* r=reactor.Get();
        auto* shell=is_uobject_valid_fast(a)?try_read_reflected_object_property_value(a,L"MemoryShell"):nullptr;
        auto* target=is_uobject_valid_fast(r)?try_read_reflected_object_property_value(r,L"m_CmdTargetActor"):nullptr;
        int delegate_count=0;
        if(is_uobject_valid_fast(r)) {
            auto* p=r->GetPropertyByNameInChain(L"OnReactorDoCommand");
            if(p&&p->IsA<FMulticastDelegateProperty>()&&p->GetElementSize()==sizeof(FMulticastScriptDelegate))
                delegate_count=p->ContainerPtrToValuePtr<FMulticastScriptDelegate>(r)->InvocationList.Num();
        }
        std::ostringstream s;s<<std::boolalpha<<'{'<<identity()<<",\"asset_ready\":"<<(klass(chest,native)!=nullptr&&(!native||(loaded(native_mesh)&&loaded(native_diffuse)&&loaded(native_open))))
            <<",\"class_ready\":"<<(klass(chest,native)!=nullptr)
            <<",\"mesh_ready\":"<<(loaded(native_mesh)!=nullptr)
            <<",\"diffuse_ready\":"<<(loaded(native_diffuse)!=nullptr)
            <<",\"animation_ready\":"<<(loaded(native_open)!=nullptr)
            <<",\"kind\":\""<<(native?"native_chest":chest?"chest":"dome")<<"\",\"chest_open\":"<<opened
            <<",\"native_mesh\":\""<<(native?json_escape(native_mesh):"")<<"\",\"animation_position\":"<<animation_position<<",\"animation_length\":"<<animation_length
            <<",\"preview\":"<<preview<<",\"preview_position\":"<<vec_json(preview_position)<<",\"preview_yaw\":"<<preview_yaw
            <<",\"lid_pitch\":"<<lid_pitch<<",\"opening_wait_for_focus\":"<<opening_pending
            <<",\"active\":"<<(active&&is_uobject_valid_fast(a))<<",\"cleared\":"<<cleared
            <<",\"actor\":\""<<(is_uobject_valid_fast(a)?json_escape(a->GetPathName()):"")
            <<"\",\"world\":\""<<(is_uobject_valid_fast(owner_world.Get())?json_escape(owner_world.Get()->GetPathName()):"")
            <<"\",\"center\":"<<vec_json(origin)<<",\"radius\":"<<(chest?0:3000)<<",\"crystal_position\":"
            <<vec_json({origin[0]+350,origin[1],origin[2]+(chest?60:150)})
            <<",\"chest_position\":"<<vec_json(preview?preview_position:Vec{origin[0]+350,origin[1],origin[2]})
            <<",\"reactor_active\":"<<(is_uobject_valid_fast(r)&&native_bool(r,L"BP_IsGimmickComponentActive"))
            <<",\"collision_enabled\":"<<(is_uobject_valid_fast(a)&&native_bool(a,L"GetActorEnableCollision"))
            <<",\"shell\":\""<<(is_uobject_valid_fast(shell)?json_escape(shell->GetPathName()):"")
            <<"\",\"target_is_barrier\":"<<(is_uobject_valid_fast(a)&&target==a)<<",\"delegate_count\":"<<delegate_count
            <<",\"interaction_count\":"<<interactions<<",\"callback_count\":"<<callback_count<<",\"error\":\""<<json_escape(std::wstring(error.begin(),error.end()))
            <<"\",\"sweep\":"<<sweep<<",\"utc_ms\":"<<unix_ms()<<'}';return s.str();
    }
    void publish() { if(auto dir=config.bridge_dir())write_text_file_atomic_verified(*dir/channels::runtime_mom_barrier_state_file,snapshot()); }
    void remove(bool solved=false) {
        auto* a=actor.Get();
        if(is_uobject_valid_fast(a)) {
            if(is_uobject_valid_fast(reactor.Get())) {Call off(reactor.Get(),L"BP_DeactivateGimmickComponent");off.run();}
            Call collision(a,L"SetActorEnableCollision");collision.boolean(L"bNewActorEnableCollision",false);collision.run();
            Call destroy(a,L"K2_DestroyActor");destroy.run();
        }
        actor=nullptr;reactor=nullptr;active=false;cleared=solved;
        opening_pending=opening=opened=false;lid_pitch=0;publish();
    }
    void unlock_chest() {
        component(native?L"NativeChestMesh":L"ChestLidPivot");
        Call off(reactor.Get(),L"BP_DeactivateGimmickComponent");off.run();
        active=false;cleared=true;opening_pending=true;publish();
    }
    void animate_chest() {
        if(opening_pending) {
            DWORD foreground_pid{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
            if(foreground_pid!=GetCurrentProcessId()||!config.gameplay_ready())return;
            opening_pending=false;opening=true;opening_start=Clock::now();
        }
        if(!opening)return;
        const float t=std::clamp(std::chrono::duration<float>(Clock::now()-opening_start).count()/1.25f,0.f,1.f);
        if(native) {
            pose_native(t);
            if(t>=1.f) {opening=false;opened=true;error.clear();publish();}
            return;
        }
        const float pitch=-85.f*t*t*(3.f-2.f*t);
        auto* pivot=component(L"ChestLidPivot");
        Call rotate(pivot,L"K2_SetRelativeRotation");vector(rotate,L"NewRotation",{pitch,0,0});
        rotate.boolean(L"bSweep",false);rotate.boolean(L"bTeleport",true);rotate.run();
        auto* property=pivot->GetPropertyByNameInChain(L"RelativeRotation");
        if(!property||!property->IsA<FStructProperty>()||property->GetElementSize()!=12)throw std::runtime_error("mom_chest_rotation_layout");
        Vec rotation{};std::memcpy(rotation.data(),property->ContainerPtrToValuePtr<void>(pivot),12);lid_pitch=rotation[0];
        if(t>=1.f) {
            if(std::abs(lid_pitch+85.f)>.5f)throw std::runtime_error("mom_chest_open_readback");
            opening=false;opened=true;error.clear();publish();
        }
    }
    static void checked(UObject* context,FFrame& stack,void*) {
        auto* self=instance;auto* f=reinterpret_cast<FFrame_50_AndBelow*>(&stack);
        if(!self)return;
        ++self->callback_count;
        if(!self->active||self->actor.Get()!=context||f->PreviousFrame||!f->Locals) {
            self->error="mom_callback_frame_or_owner";return;
        }
        try {
            UObject* causer{};UObject* r{};
            std::memcpy(&causer,f->Locals,8);std::memcpy(&r,reinterpret_cast<std::byte*>(f->Locals)+16,8);
            auto* pawn=self->config.playable_sora();
            if(causer!=pawn||r!=self->reactor.Get()||world(pawn)!=self->owner_world.Get()||
                !self->config.gameplay_ready()||Clock::now()<self->cooldown) {
                self->error="mom_callback_context_or_cooldown";return;
            }
            const auto p=position(pawn);const Vec marker{self->origin[0]+350,self->origin[1],self->origin[2]+(self->chest?60:100)};
            float distance=0;for(int i=0;i<3;++i)distance+=(p[i]-marker[i])*(p[i]-marker[i]);
            if(distance>barrier_interaction_radius*barrier_interaction_radius)return;
            auto dir=self->config.bridge_dir();if(!dir)return;
            if(!begin_music_chest_pause(pawn))return;
            ++self->interactions;self->cooldown=Clock::now()+barrier_interaction_cooldown;
            std::ostringstream message;message<<'{'<<self->identity()<<",\"sequence\":"<<self->interactions
                <<",\"expires_unix_ms\":"<<unix_ms()+15000<<'}';
            write_text_file_atomic_verified(*dir/channels::runtime_mom_barrier_interaction_file,message.str());
            auto* library=UObjectGlobals::StaticFindObject<UObject*>(nullptr,nullptr,L"/Script/TresGame.Default__TresGameBlueprintLibrary");
            Call notice(library,L"TresUI_OpenInformation");
            const std::array labels{L"Beginner",L"Standard",L"Proud"};
            const std::wstring text=std::wstring(self->chest?L"Melody Chest: ":L"Memory Seal: ")+self->song_name+L" ("+labels[self->difficulty]+L"). Clear the song in Melody of Memory.";
            if(!write_tres_loc_text_function_param(notice.field(L"InformationText"),notice.data.data(),text,self->message_storage))throw std::runtime_error("mom_notice_layout");
            notice.boolean(L"AutoClose",true);
            write_float_function_param(notice.field(L"OptionalDisplayTime"),notice.data.data(),5.f);
            Call clear(library,L"TresUI_ClearInformationTextCache");clear.run();notice.run();
            retain_runtime_ap_message_params(self->message_storage,std::move(notice.data));self->publish();
        } catch(const std::exception& e) {self->error=e.what();}
    }
    void spawn_preview(UObject* pawn,const std::string& request) {
        if(active||is_uobject_valid_fast(actor.Get()))throw std::runtime_error("remove_existing_mom_barrier_first");
        if(!is_uobject_valid_fast(world(pawn)))throw std::runtime_error("preview_world_required");
        Vec marker{};
        const char* fields[]={"x","y","z"};
        for(int i=0;i<3;++i) {
            auto value=JsonFieldReader::number_field(request,fields[i]);
            if(!value||!std::isfinite(*value)||std::abs(*value)>max_barrier_preview_coordinate)throw std::runtime_error("preview_position_invalid");
            marker[i]=static_cast<float>(*value);
        }
        auto yaw=JsonFieldReader::number_field(request,"yaw");
        if(!yaw||!std::isfinite(*yaw)||std::abs(*yaw)>max_barrier_preview_yaw_degrees)throw std::runtime_error("preview_yaw_invalid");
        if(!klass(true,true)||!loaded(native_mesh)||!loaded(native_diffuse)||!loaded(native_open))
            throw std::runtime_error("prepare_native_chest_first");
        native=chest=preview=true;preview_yaw=static_cast<float>(*yaw);
        // The prototype visual is 350 units along local X. Trace at the marker,
        // then offset the actor root so its rotated visual lands on that marker.
        preview_position=marker;
        preview_position[2]=chest_ground(pawn,{marker[0]-350,marker[1],marker[2]});
        const double angle=*yaw*3.141592653589793/180;
        origin={preview_position[0]-float(350*std::cos(angle)),preview_position[1]-float(350*std::sin(angle)),preview_position[2]};
        const FTransform transform{FQuat{0,0,float(std::sin(angle/2)),float(std::cos(angle/2))},
            FVector{origin[0],origin[1],origin[2]},FVector{1,1,1}};
        auto* a=UGameplayStatics::BeginDeferredActorSpawnFromClass(pawn,klass(true,true),transform,ESpawnActorCollisionHandlingMethod::AlwaysSpawn,nullptr);
        if(!is_uobject_valid_fast(a))throw std::runtime_error("preview_spawn_failed");
        try {
            auto* finished=UGameplayStatics::FinishSpawningActor(a,transform);
            if(is_uobject_valid_fast(finished))a=finished;
            if(!is_uobject_valid_fast(a))throw std::runtime_error("preview_finish_failed");
            auto* r=try_read_reflected_object_property_value(a,L"MemoryReactor");
            if(!is_uobject_valid_fast(r))throw std::runtime_error("preview_reactor_missing");
            // Native delegate binding allocates the actor weak serial, as in the
            // established prototype spawn path. Keep the reactor inactive.
            Call bind(a,L"BindToReactor");
            if(!write_object_function_param(bind.fn,bind.data.data(),L"ReactorComponent",r))throw std::runtime_error("preview_bind_layout");
            bind.run();actor=a;owner_world=world(pawn);reactor=r;
            if(actor.Get()!=a)throw std::runtime_error("preview_actor_weak_identity_failed");
            if(reactor.Get()!=r)throw std::runtime_error("preview_reactor_weak_identity_failed");
            Call off(reactor.Get(),L"BP_DeactivateGimmickComponent");off.run();
            Call collision(a,L"SetActorEnableCollision");collision.boolean(L"bNewActorEnableCollision",false);collision.run();
            setup_native();
            Call location(component(L"NativeChestMesh"),L"K2_GetComponentLocation");location.run();
            auto* field=location.field(L"ReturnValue");
            if(!field->IsA<FStructProperty>()||field->GetElementSize()!=12)throw std::runtime_error("preview_location_layout");
            Vec observed{};std::memcpy(observed.data(),field->ContainerPtrToValuePtr<void>(location.data.data()),12);
            for(int i=0;i<3;++i)if(!std::isfinite(observed[i])||std::abs(observed[i]-preview_position[i])>1)
                throw std::runtime_error("preview_location_readback");
            preview_position=observed;active=true;cleared=opening_pending=opening=opened=false;
            id=L"placement_preview";seed.clear();slot=location_id=interactions=0;error.clear();publish();
        } catch(...) {
            if(is_uobject_valid_fast(a)){Call destroy(a,L"K2_DestroyActor");destroy.run();}
            actor=nullptr;reactor=nullptr;active=false;throw;
        }
    }
    void spawn(UObject* pawn,const std::string& request) {
        if(active||is_uobject_valid_fast(actor.Get()))throw std::runtime_error("remove_existing_mom_barrier_first");
        const auto kind=JsonFieldReader::string_field(request,"kind").value_or(L"dome");
        if(kind!=L"dome"&&kind!=L"chest"&&kind!=L"native_chest")throw std::runtime_error("unknown_mom_kind");
        preview=false;native=kind==L"native_chest";chest=kind!=L"dome";
        if(!klass(chest,native))throw std::runtime_error("prepare_mom_asset_first");
        if(!world(pawn)||world(pawn)->GetPathName()!=L"/Game/Levels/ra/ra_01/ra_01.ra_01")throw std::runtime_error("mom_test_requires_corona");
        id=JsonFieldReader::string_field(request,"barrier_id").value_or(L"");
        seed=JsonFieldReader::string_field(request,"seed_name").value_or(L"");
        slot=JsonFieldReader::int64_field(request,"slot").value_or(0);
        location_id=JsonFieldReader::int64_field(request,"ap_location_id").value_or(0);
        difficulty=static_cast<int>(JsonFieldReader::int64_field(request,"difficulty").value_or(0));
        song_name=JsonFieldReader::string_field(request,"song_name").value_or(L"");
        if(id.size()!=36||seed.empty()||slot<1||location_id<1||difficulty<0||difficulty>2||song_name.empty())throw std::runtime_error("mom_challenge_identity_required");
        origin=position(pawn);owner_world=world(pawn);cleared=false;interactions=0;error.clear();sweep="null";
        opening_pending=opening=opened=false;lid_pitch=0;animation_position=animation_length=0;
        if(chest)origin[2]=chest_ground(pawn,origin);
        const auto callback=std::wstring(visual_asset(chest,native))+L":OnBarrierChecked";
        auto* fn=UObjectGlobals::StaticFindObject<UFunction*>(nullptr,nullptr,
            callback.c_str());
        if(!is_uobject_valid_fast(fn)||fn->GetNumParms()!=3||fn->GetParmsSize()!=24)throw std::runtime_error("mom_delegate_signature");
        constexpr std::array names{L"CommandCauser",L"CommandID",L"ReactorComponent"};
        constexpr std::array offsets{0,8,16};constexpr std::array sizes{8,1,8};
        for(size_t i=0;i<names.size();++i) {
            auto* p=find_function_param_by_name(fn,names[i]);
            if(!p||p->GetOffset_Internal()!=offsets[i]||p->GetElementSize()!=sizes[i])
                throw std::runtime_error("mom_delegate_parameter_layout");
        }
        const FTransform transform{FQuat{0,0,0,1},FVector{origin[0],origin[1],origin[2]},FVector{1,1,1}};
        auto* a=UGameplayStatics::BeginDeferredActorSpawnFromClass(pawn,klass(chest,native),transform,ESpawnActorCollisionHandlingMethod::AlwaysSpawn,nullptr);
        if(!is_uobject_valid_fast(a))throw std::runtime_error("mom_spawn_failed");
        auto* finished=UGameplayStatics::FinishSpawningActor(a,transform);
        if(is_uobject_valid_fast(finished))a=finished;
        if(!is_uobject_valid_fast(a))throw std::runtime_error("mom_finish_spawn_failed");
        try {
            auto* r=try_read_reflected_object_property_value(a,L"MemoryReactor");
            if(!is_uobject_valid_fast(r))throw std::runtime_error("mom_reactor_missing");
            auto* target=r->GetPropertyByNameInChain(L"m_CmdTargetActor");
            if(!target||target->GetElementSize()!=sizeof(UObject*))throw std::runtime_error("mom_target_layout");
            *target->ContainerPtrToValuePtr<UObject*>(r)=a;
            Call bind(a,L"BindToReactor");
            if(!write_object_function_param(bind.fn,bind.data.data(),L"ReactorComponent",r))throw std::runtime_error("mom_bind_layout");
            bind.run();actor=a;reactor=r;
            fn->SetFuncPtr(&Impl::checked);fn->GetFunctionFlags()|=FUNC_Native;
            auto* delegate_field=r->GetPropertyByNameInChain(L"OnReactorDoCommand");
            if(!delegate_field||!delegate_field->IsA<FMulticastDelegateProperty>()||
                delegate_field->GetElementSize()!=sizeof(FMulticastScriptDelegate))throw std::runtime_error("mom_delegate_layout");
            auto& listeners=delegate_field->ContainerPtrToValuePtr<FMulticastScriptDelegate>(r)->InvocationList;
            if(listeners.Num()!=1||listeners[0].Object.Get()!=a||listeners[0].FunctionName.ToString()!=L"OnBarrierChecked")
                throw std::runtime_error("mom_delegate_binding_readback");
            if(actor.Get()!=a||reactor.Get()!=r)throw std::runtime_error("mom_weak_identity_failed");
            if(native)setup_native();
            else if(chest)component(L"ChestLidPivot");
            Call activate(r,L"BP_ActivateGimmickComponent");activate.run();active=true;publish();
        } catch(...) {Call destroy(a,L"K2_DestroyActor");destroy.run();actor=nullptr;reactor=nullptr;active=false;throw;}
    }
    void probe(UObject* pawn,const std::string& request) {
        if(chest)throw std::runtime_error("mom_sweep_probe_is_dome_only");
        if(!active||world(pawn)!=owner_world.Get())throw std::runtime_error("mom_barrier_required");
        const Vec before=position(pawn);Vec target=before;
        int axis=static_cast<int>(JsonFieldReader::int64_field(request,"axis").value_or(2));
        int sign=static_cast<int>(JsonFieldReader::int64_field(request,"sign").value_or(1));
        if(axis<0||axis>2||(sign!=1&&sign!=-1))throw std::runtime_error("mom_probe_axis");
        target[axis]+=sign*7000;
        if(auto yaw=JsonFieldReader::number_field(request,"yaw")) {
            auto pitch=JsonFieldReader::number_field(request,"pitch").value_or(0);
            if(!std::isfinite(*yaw)||!std::isfinite(pitch)||std::abs(*yaw)>max_barrier_preview_yaw_degrees||std::abs(pitch)>max_barrier_preview_pitch_degrees)
                throw std::runtime_error("mom_probe_angle");
            const double az=*yaw*3.141592653589793/180,el=pitch*3.141592653589793/180;
            target={before[0]+float(7000*std::cos(el)*std::cos(az)),
                    before[1]+float(7000*std::cos(el)*std::sin(az)),before[2]+float(7000*std::sin(el))};
        }
        Call move(pawn,L"K2_SetActorLocation");vector(move,L"NewLocation",target);
        move.boolean(L"bSweep",true);move.boolean(L"bTeleport",true);move.run();
        Vec after=position(pawn);
        // Restore the inspected start immediately, even if result reflection fails.
        Call restore(pawn,L"K2_SetActorLocation");vector(restore,L"NewLocation",before);
        restore.boolean(L"bSweep",false);restore.boolean(L"bTeleport",true);restore.run();
        auto* p=move.field(L"SweepHitResult");
        if(!p->IsA<FStructProperty>())throw std::runtime_error("mom_hit_layout");
        auto* structure=static_cast<FStructProperty*>(p)->GetStruct();
        auto* actor_field=find_struct_field_by_name(structure,L"Actor");
        auto* blocking=find_struct_field_by_name(structure,L"bBlockingHit");
        auto* storage=p->ContainerPtrToValuePtr<void>(move.data.data());
        if(!actor_field||actor_field->GetElementSize()!=sizeof(FWeakObjectPtr)||!blocking||!blocking->IsA<FBoolProperty>())throw std::runtime_error("mom_hit_actor_layout");
        auto* hit=actor_field->ContainerPtrToValuePtr<FWeakObjectPtr>(storage)->Get();
        bool blocked=static_cast<FBoolProperty*>(blocking)->GetPropertyValueInContainer(storage);
        std::ostringstream s;s<<std::boolalpha<<"{\"start\":"<<vec_json(before)<<",\"target\":"<<vec_json(target)
            <<",\"reached\":"<<vec_json(after)<<",\"restored\":"<<vec_json(position(pawn))<<",\"blocking_hit\":"<<blocked
            <<",\"hit_actor\":\""<<(is_uobject_valid_fast(hit)?json_escape(hit->GetPathName()):"")
            <<"\",\"hit_barrier\":"<<(hit==actor.Get()&&blocked)<<'}';sweep=s.str();publish();
    }
    void tick() {
        if(!active&&!is_uobject_valid_fast(actor.Get()))return;
        try {
            if(opening||opening_pending)animate_chest();
            if(Clock::now()<next)return;next=Clock::now()+barrier_request_update_interval;
            auto* pawn=config.playable_sora();
            if(!is_uobject_valid_fast(actor.Get())) {active=false;publish();return;}
            if(is_uobject_valid_fast(pawn)&&world(pawn)!=owner_world.Get()) {remove();return;}
            if(!active||preview)return;
            if(auto dir=config.bridge_dir())if(auto text=read_text_file(*dir/channels::runtime_mom_barrier_unlock_file)) {
                if(JsonFieldReader::string_field(*text,"schema")==L"kh3ap.mom_barrier.v1"&&
                    JsonFieldReader::string_field(*text,"barrier_id")==id&&
                    JsonFieldReader::int64_field(*text,"process_id")==GetCurrentProcessId()&&
                    JsonFieldReader::string_field(*text,"seed_name")==seed&&
                    JsonFieldReader::int64_field(*text,"slot")==slot&&
                    JsonFieldReader::int64_field(*text,"ap_location_id")==location_id&&
                    JsonFieldReader::bool_field(*text,"cleared")==true) {
                    if(chest)unlock_chest();else remove(true);
                }
            }
        } catch(const std::exception& e) {error=e.what();}
    }
};
RuntimeMoMBarrierService::RuntimeMoMBarrierService(RuntimeEnemyClearConfig c):impl_(std::make_unique<Impl>(std::move(c))) {}
RuntimeMoMBarrierService::~RuntimeMoMBarrierService()=default;
void RuntimeMoMBarrierService::tick() {impl_->tick();}
std::string RuntimeMoMBarrierService::command(UObject* pawn,const std::string& request) {
    auto action=JsonFieldReader::string_field(request,"action").value_or(L"inspect");
    if(action==L"prepare") {
        const auto kind=JsonFieldReader::string_field(request,"kind").value_or(L"dome");
        if(kind!=L"dome"&&kind!=L"chest"&&kind!=L"native_chest")throw std::runtime_error("unknown_mom_kind");
        if(is_uobject_valid_fast(impl_->actor.Get())&&(impl_->chest!=(kind!=L"dome")||impl_->native!=(kind==L"native_chest")))throw std::runtime_error("remove_existing_mom_barrier_first");
        impl_->chest=kind!=L"dome";impl_->native=kind==L"native_chest";
        if(!klass(impl_->chest,impl_->native)&&!request_runtime_asset_class_load(pawn,visual_asset(impl_->chest,impl_->native),nullptr,0x4b484710))throw std::runtime_error("mom_load_request_failed");
        if(impl_->native) {
            int32_t uuid=0x4b484711;
            for(auto path:{native_mesh,native_diffuse,native_open}) {
                if(!loaded(path)&&!request_runtime_asset_load(pawn,path,nullptr,uuid))throw std::runtime_error("mom_native_load_failed");
                ++uuid;
            }
        }
    }
    else if(action==L"spawn_preview")impl_->spawn_preview(pawn,request);
    else if(action==L"spawn")impl_->spawn(pawn,request);
    else if(action==L"remove")impl_->remove();
    else if(action==L"probe")impl_->probe(pawn,request);
    else if(action!=L"inspect")throw std::runtime_error("unknown_mom_barrier_action");
    return impl_->snapshot();
}
}
