#include "kh3ap/features/world/save_slot_policy.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/world/space/save_space_service.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/world/space/space_profile.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/world/garden_return/garden_return_policy.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/session/command_guard.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <array>
#include <cstring>
#include <sstream>
#include <vector>

using namespace RC;
using namespace RC::Unreal;
namespace kh3ap {
namespace {
        constexpr auto space_save_selection_timeout = std::chrono::seconds(10);
        constexpr auto space_save_diagnostic_interval = std::chrono::milliseconds(200);

constexpr int32_t anchor=1902;
constexpr wchar_t button_name[]=L"kh3apReturnToSpace";
constexpr GfxProbeFunctionPaths gfx{L"/Script/ScaleformUI.GFxObject:GetMemberValue",
    L"/Script/ScaleformUI.GFxObject:Invoke",L"/Script/ScaleformUI.GFxObject:SetMemberValue"};
template<class T> T field(void* p,size_t offset) {
    __try { return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+offset); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return {}; }
}
UObject* object(UObject* p,const wchar_t* name) {
    if (!is_uobject_valid_fast(p)) return nullptr;
    auto* value=try_read_reflected_object_property_value(p,name);
    return is_uobject_valid_fast(value)?value:nullptr;
}
UObject* viewport() { return object(UObjectGlobals::StaticFindObject<UObject*>(nullptr,nullptr,
    L"/Engine/Transient.TresGameEngine_0"),L"GameViewport"); }
std::wstring world() { auto* w=object(viewport(),L"World");return w?w->GetFullName():L""; }
UObject* menu() { return object(object(object(viewport(),L"GameInstance"),L"m_UIManager"),L"m_SaveLoad"); }
UObject* proxy(UObject* m,int i) { return is_uobject_valid_fast(m)&&i>=0&&i<6?field<UObject*>(m,0x458+i*8):nullptr; }
UObject* row(UObject* m,int i) { return object(proxy(m,i),L"m_ASObject"); }
UObject* parent(UObject* m) { auto* r=row(m,0);return r?try_gfx_get_member_value_object(gfx,r,L"parent"):nullptr; }
UObject* custom(UObject* m) {
    for(int i=0;i<6;++i){auto* r=row(m,i);if(r&&try_gfx_get_member_value_number(gfx,r,L"userParam")==anchor)return r;}
    auto* p=parent(m);return p?try_gfx_invoke_string_arg_object(gfx,p,L"getChildByName",button_name):nullptr;
}
UObject* child(UObject* source,UObject* source_child,UObject* clone) {
    if (!source||!source_child||!clone)return nullptr;
    const auto name=try_gfx_get_member_value_string(gfx,source_child,L"name");
    auto* p=try_gfx_get_member_value_object(gfx,source_child,L"parent");
    const auto pn=p?try_gfx_get_member_value_string(gfx,p,L"name"):std::nullopt;
    const auto sn=try_gfx_get_member_value_string(gfx,source,L"name");
    if (!name||!pn||!sn)return nullptr;
    auto* cp=*pn==*sn?clone:try_gfx_invoke_string_arg_object(gfx,clone,L"getChildByName",*pn);
    return cp?try_gfx_invoke_string_arg_object(gfx,cp,L"getChildByName",*name):nullptr;
}
bool area_menu(){
    return camp_is_kh02_world(world())&&Kh02Story::challenges_available();
}
}
struct RuntimeSaveSpaceService::Impl {
    using Clock=std::chrono::steady_clock;
    using Input=bool(*)(UObject*,void*);
    inline static Impl* active{};
    std::function<std::optional<std::filesystem::path>()> bridge;
    std::unique_ptr<PLH::x64Detour> hook;
    uint64_t original{};
    uintptr_t input_rva{},base{};
    bool attempted{},installed{},pending{},editing{};
    int pending_area=-1;
    FWeakObjectPtr pending_menu;
    Clock::time_point selected_at{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    Clock::time_point next_diagnostic{};
#endif
    std::wstring source,phase=L"idle";
    explicit Impl(decltype(bridge) b):bridge(std::move(b)) {}
    ~Impl(){if(hook&&hook->isHooked())hook->unHook();if(active==this)active=nullptr;}
    bool valid_menu(UObject* m) const {
        return is_uobject_valid_fast(m)&&m->GetClassPrivate()->GetName()==L"TresUIP_SaveLoad"&&
            field<uintptr_t>(reinterpret_cast<void*>(field<uintptr_t>(m,0)),0x2c8)==base+input_rva;
    }
    bool ready(UObject* m) const {
        auto* top=object(m,L"m_SaveTopRoot");
        return valid_menu(m)&&field<uint8_t>(m,0xa1)&&field<uint8_t>(m,0xa2)&&top&&
            try_gfx_get_member_value(gfx,top,L"visible")==L"ASType=2, ASBoolean=true";
    }
    void add_row(UObject* m) {
        if(editing||pending||!valid_menu(m)||!object(m,L"m_SaveTopRoot")||custom(m))return;
        std::vector<UObject*> rows;int garden=-1;
        for(int i=0;i<6;++i) {
            auto* r=row(m,i);
            if(!r||try_gfx_get_member_value(gfx,r,L"visible")!=L"ASType=2, ASBoolean=true")continue;
            const auto command=try_gfx_get_member_value_number(gfx,r,L"userParam");
            if(command&&*command==2)garden=static_cast<int>(rows.size());
            rows.push_back(r);
        }
        const bool area=area_menu();
        // Reuse the stock area row when present: a second travel entry would
        // expose KH3's unrelated area catalog alongside the private 0.2 list.
        if(area)for(int i=0;i<6;++i){
            auto* r=row(m,i);
            if(!r||try_gfx_get_member_value_number(gfx,r,L"userParam")!=1)continue;
            try_gfx_set_member_value_int(gfx,r,L"userParam",anchor);
            try_gfx_set_member_value_string(gfx,r,L"label",L"Area Select");
            if(auto* help=object(proxy(m,i),L"m_HelpMessage"))try_gfx_set_member_value_string(gfx,help,L"text",L"Travel to an area you have visited in Kingdom Hearts 0.2.");
            return;
        }
        if(area&&garden<0&&!rows.empty())garden=static_cast<int>(rows.size())-1;
        if(garden<0||rows.size()<2||space::is_space(world()))return;
        auto* original_row=rows[garden];auto* container=parent(m);
        const auto index=try_gfx_get_member_value_number(gfx,original_row,L"index");
        const auto y=try_gfx_get_member_value_number(gfx,rows[0],L"y");
        const auto next_y=try_gfx_get_member_value_number(gfx,rows[1],L"y");
        if(!container||!index||!y||!next_y)return;
        editing=true;
        try_gfx_set_member_value_int(gfx,original_row,L"index",-1);
        auto* extra=try_gfx_invoke_no_args_object(gfx,original_row,L"clone");
        try_gfx_set_member_value_int(gfx,original_row,L"index",static_cast<int32_t>(*index));
        if(!extra){editing=false;return;}
        try_gfx_set_member_value_string(gfx,extra,L"name",button_name);
        try_gfx_set_member_value_int(gfx,extra,L"index",anchor);
        try_gfx_set_member_value_int(gfx,extra,L"userParam",anchor);
        try_gfx_set_member_value_string(gfx,extra,L"label",area?L"Area Select":L"Return to Space");
        try_gfx_set_member_value_bool(gfx,extra,L"selected",false);
        try_gfx_set_member_value_bool(gfx,extra,L"enabled",true);
        try_gfx_set_member_value_bool(gfx,extra,L"visible",true);
        if(!try_gfx_invoke_object_arg(gfx,container,L"addChild",extra)){editing=false;return;}
        for(int i=0;i<6;++i)if(row(m,i)==original_row) {
            auto* help=child(original_row,object(proxy(m,i),L"m_HelpMessage"),extra);
            if(help)try_gfx_set_member_value_string(gfx,help,L"text",area?L"Travel to an area you have visited in Kingdom Hearts 0.2.":L"Return to the space overview.");
            auto* source_icon=object(proxy(m,i),L"m_CategoryIcon");
            auto* icon=child(original_row,source_icon,extra);
            const auto frame=source_icon?try_gfx_get_member_value_number(gfx,source_icon,L"currentFrame"):std::nullopt;
            if(icon&&frame)try_gfx_invoke_int_arg(gfx,icon,L"gotoAndStop",static_cast<int32_t>(*frame));
        }
        if(auto x=try_gfx_get_member_value_number(gfx,original_row,L"x"))try_gfx_set_member_value_number(gfx,extra,L"x",*x);
        rows.insert(rows.begin()+garden+1,extra);
        const double scale=rows.size()>6?6.0/rows.size():1.0;
        const double step=(*next_y-*y)*scale;
        for(size_t i=0;i<rows.size();++i) {
            auto* r=rows[i];
            try_gfx_set_member_value_number(gfx,r,L"y",*y+i*step);
            if(scale<1) {
                try_gfx_set_member_value_number(gfx,r,L"scaleX",scale);
                try_gfx_set_member_value_number(gfx,r,L"scaleY",scale);
            }
            const auto up=try_gfx_get_member_value_number(gfx,rows[(i+rows.size()-1)%rows.size()],L"index");
            const auto down=try_gfx_get_member_value_number(gfx,rows[(i+1)%rows.size()],L"index");
            if(up)try_gfx_set_member_value_int(gfx,r,L"toUp",static_cast<int32_t>(*up));
            if(down)try_gfx_set_member_value_int(gfx,r,L"toDown",static_cast<int32_t>(*down));
            try_gfx_set_member_value_bool(gfx,r,L"repeatStopUp",i==0);
            try_gfx_set_member_value_bool(gfx,r,L"repeatStopDown",i+1==rows.size());
        }
        editing=false;
        Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] SaveSpace added custom row area={} rows={}\n"),area,rows.size());
    }
    static bool input(UObject* m,void* event) {
        auto& self=*active;
        const int kind=field<int32_t>(event,0x10),parameter=field<int32_t>(event,0x18);
        if(parameter==anchor&&self.valid_menu(m)) {
            if(kind==7&&self.ready(m)&&!self.pending&&custom(m)) {
                if(area_menu()){
                    const auto selected_menu=make_uobject_weak(m);
                    Kh02Story::open_area_select(m,[selected_menu](int area){
                        auto* s=active;auto* target=selected_menu.Get();
                        if(!s||!s->ready(target)||!area_menu())return;
                        s->source=world();s->selected_at=Clock::now();s->pending=true;s->pending_menu=target;s->pending_area=area;s->phase=L"closing_for_area_select";
                        alignas(16) std::array<std::byte,0x30> cancel{};
                        *reinterpret_cast<int32_t*>(cancel.data()+0x10)=8;
                        reinterpret_cast<Input>(s->original)(target,cancel.data());
                    });
                    *reinterpret_cast<uint32_t*>(static_cast<std::byte*>(event)+0x24)|=2;return true;
                }
                self.pending_area=-1;
                self.source=world();self.selected_at=Clock::now();self.pending=true;self.pending_menu=m;self.phase=L"closing_save_menu";
                // Use the normal top-level Cancel path, including its animation,
                // completion delegate and player-state teardown.
                alignas(16) std::array<std::byte,0x30> cancel{};
                std::memcpy(cancel.data(),event,cancel.size());
                *reinterpret_cast<int32_t*>(cancel.data()+0x10)=8;
                *reinterpret_cast<int32_t*>(cancel.data()+0x18)=0;
                reinterpret_cast<Input>(self.original)(m,cancel.data());
            } else if(kind==3||kind==8) {
                alignas(16) std::array<std::byte,0x30> copy{};
                std::memcpy(copy.data(),event,copy.size());
                *reinterpret_cast<int32_t*>(copy.data()+0x18)=kind==3?(area_menu()?1:2):0;
                reinterpret_cast<Input>(self.original)(m,copy.data());
            }
            *reinterpret_cast<uint32_t*>(static_cast<std::byte*>(event)+0x24)|=2;
            return true;
        }
        const bool result=reinterpret_cast<Input>(self.original)(m,event);
        if(kind==3&&!self.editing)self.add_row(m);
        return result;
    }
    void install() {
        if(attempted)return;attempted=true;
        const auto& p=space::native_profile();if(!p.supported)return;
        base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        input_rva=p.steam?0x1708f70:0x1a8bab0;
        constexpr std::array<uint8_t,24> egs{0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xe0,0x48,0x81,0xec,0x20,0x01,0x00,0x00};
        constexpr std::array<uint8_t,24> steam{0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xd9,0x48,0x81,0xec,0xf0,0x00,0x00,0x00};
        if(std::memcmp(reinterpret_cast<void*>(base+input_rva),(p.steam?steam:egs).data(),egs.size()))return;
        active=this;hook=std::make_unique<PLH::x64Detour>(base+input_rva,reinterpret_cast<uintptr_t>(&input),&original);
        installed=hook->hook()&&original;
        Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] SaveSpace installed={}\n"),installed);
    }
    void tick(Clock::time_point now);
};
void RuntimeSaveSpaceService::Impl::tick(Clock::time_point now) {
    if(!installed)return;
    if(pending) {
        auto* m=pending_menu.Get();
        if(world()!=source||now-selected_at>space_save_selection_timeout||!valid_menu(m)) {
            pending=false;phase=L"canceled_context_changed_or_timeout";
        } else if(!field<uint8_t>(m,0xa1)) {
            bool jumped=false;
            if(pending_area>=0)jumped=Kh02Story::travel_to_visited_area(pending_area);
            else
            jumped=RuntimeSpaceService::return_from_closed_menu(source);
            if(jumped){pending=false;phase=L"jump_dispatched";}
        }
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    if(now<next_diagnostic)return;
    next_diagnostic=now+space_save_diagnostic_interval;
    const auto dir=bridge();if(!dir)return;
    const auto path=*dir/L"runtime_save_space_request.json";
    const auto text=read_text_file(path);if(!text)return;
    const auto id=JsonFieldReader::string_field(*text,"request_id").value_or(L"");
    const auto action=JsonFieldReader::string_field(*text,"command").value_or(L"inspect");
    auto reason=std::wstring(runtime_command_request_failure(*text,GetCurrentProcessId(),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
    auto* m=menu();
    if(reason.empty()&&action!=L"inspect"&&JsonFieldReader::string_field(*text,"expected_world").value_or(L"")!=world())reason=L"expected_world_mismatch";
    if(reason.empty()&&(action==L"area_next"||action==L"area_confirm"||action==L"area_back")){
        if(!area_menu()||!Kh02Story::challenges_open())reason=L"area_select_required";
        else {const auto* key=action==L"area_next"?L"Down":action==L"area_confirm"?L"Enter":L"Escape";
            Kh02Story::route_challenges(0,key,0,false);Kh02Story::route_challenges(0,key,1,false);}
    }else if(reason.empty()&&action==L"focus") {
        auto* root=object(object(m,L"m_ASProxyUIComponent"),L"m_ASObject");
        const auto index=custom(m)?try_gfx_get_member_value_number(gfx,custom(m),L"index"):std::nullopt;
        if(!ready(m)||!index||!root||!try_gfx_invoke_int_arg(gfx,root,L"focusAnchor",static_cast<int32_t>(*index)))reason=L"ready_save_menu_required";
    } else if(reason.empty()&&(action==L"save_open"||action==L"save_slot")) {
        // Agent-only UI exercise: use the actual Save row and its list event,
        // never the Sora-only direct-save service or a synthetic completion.
        auto* controller=object(object(m,L"m_pUIManager"),L"m_pPlayerController");
        auto* pawn=object(controller,L"Pawn");
        auto* state=object(object(pawn,L"MyStateComp"),L"m_pCurrentState");
        if(!character_swap::owns_swap_pawn(pawn)||character_swap::current_hero()!=pawn||
            !state||state->GetClassPrivate()->GetName()!=L"TresPlayerState_SaveMenu") {
            reason=L"owned_alternate_native_save_menu_required";
        } else if(action==L"save_open") {
            auto* save_row=row(m,0);
            if(!ready(m)||!save_row||try_gfx_get_member_value_number(gfx,save_row,L"userParam")!=0||
                try_gfx_get_member_value(gfx,save_row,L"enabled")!=L"ASType=2, ASBoolean=true") {
                reason=L"enabled_save_row_required";
            } else {
                alignas(16) std::array<std::byte,0x30> event{};
                *reinterpret_cast<int32_t*>(event.data()+0x10)=7;
                *reinterpret_cast<int32_t*>(event.data()+0x14)=11;
                *reinterpret_cast<int32_t*>(event.data()+0x18)=0;
                if(!reinterpret_cast<Input>(original)(m,event.data())||!(field<uint32_t>(event.data(),0x24)&2))
                    reason=L"native_save_row_rejected";
            }
        } else {
            const auto slot=JsonFieldReader::int64_field(*text,"slot").value_or(-1);
            auto* list=object(object(m,L"m_SaveDataList"),L"m_ASObject");
            auto* root=object(m,L"m_SaveListRoot");
            if(!save_slots::is_agent_test_slot(slot)||!valid_menu(m)||!field<uint8_t>(m,0xa2)||!list||!root||
                try_gfx_get_member_value(gfx,root,L"visible")!=L"ASType=2, ASBoolean=true"||
                try_gfx_get_member_value_number(gfx,list,L"userParam")!=7) {
                reason=L"native_save_list_and_test_slot_required";
            } else {
                alignas(16) std::array<std::byte,0x30> event{};
                *reinterpret_cast<UObject**>(event.data()+8)=list;
                *reinterpret_cast<int32_t*>(event.data()+0x10)=7;
                *reinterpret_cast<int32_t*>(event.data()+0x18)=7;
                *reinterpret_cast<int32_t*>(event.data()+0x28)=static_cast<int32_t>(slot)-1;
                if(!reinterpret_cast<Input>(original)(m,event.data())||!(field<uint32_t>(event.data(),0x24)&2))
                    reason=L"native_save_slot_rejected";
            }
        }
    } else if(reason.empty()&&action==L"cancel") {
        if(!ready(m)||Kh02Story::challenges_open()) {
            reason=L"ready_save_menu_required";
        } else {
            alignas(16) std::array<std::byte,0x30> cancel{};
            *reinterpret_cast<int32_t*>(cancel.data()+0x10)=8;
            reinterpret_cast<Input>(original)(m,cancel.data());
        }
    } else if(reason.empty()&&(action==L"select"||action==L"area_open")) {
        if(!ready(m)||!custom(m))reason=L"ready_save_menu_required";
        else {alignas(16) std::array<std::byte,0x30> e{};
            *reinterpret_cast<int32_t*>(e.data()+0x10)=7;*reinterpret_cast<int32_t*>(e.data()+0x14)=anchor;
            *reinterpret_cast<int32_t*>(e.data()+0x18)=anchor;input(m,e.data());}
    } else if(reason.empty()&&action!=L"inspect")reason=L"unknown_command";
    std::ostringstream out;out<<"{\"request_id\":\""<<json_escape(id)<<"\",\"process_id\":"<<GetCurrentProcessId()
        <<",\"status\":\""<<(reason.empty()?"completed":"failed")<<"\",\"reason\":\""<<json_escape(reason)
        <<"\",\"world\":\""<<json_escape(world())<<"\",\"phase\":\""<<json_escape(phase)
        <<"\",\"ready\":"<<(ready(m)?"true":"false")<<",\"rows\":[";
    bool comma=false;
    for(int i=0;i<7;++i){auto* r=i==6?custom(m):row(m,i);if(!r)continue;
        if(i==6){bool stock=false;for(int j=0;j<6;++j)stock|=row(m,j)==r;if(stock)continue;}
        if(comma)out<<',';comma=true;out<<'{';bool c=false;
        for(auto* key:{L"name",L"label",L"index",L"userParam",L"y",L"visible",L"enabled",L"selected",L"toUp",L"toDown"}) {
            if(c)out<<',';c=true;out<<'"'<<json_escape(key)<<"\":\""<<json_escape(try_gfx_get_member_value(gfx,r,key))<<'"';}
        out<<'}';}
    auto* save_list=object(object(m,L"m_SaveDataList"),L"m_ASObject");
    out<<"],\"save_list\":{\"visible\":\""<<json_escape(try_gfx_get_member_value(gfx,object(m,L"m_SaveListRoot"),L"visible"))
       <<"\",\"user_param\":\""<<json_escape(try_gfx_get_member_value(gfx,save_list,L"userParam"))
       <<"\",\"selected_index\":\""<<json_escape(try_gfx_get_member_value(gfx,save_list,L"selectedIndex"))<<"\"}}\n";
    write_text_file_atomic_verified(*dir/L"runtime_save_space_result.json",out.str());try_remove_file(path);
#endif
}
RuntimeSaveSpaceService::RuntimeSaveSpaceService(std::function<std::optional<std::filesystem::path>()> b):impl_(std::make_unique<Impl>(std::move(b))){}
RuntimeSaveSpaceService::~RuntimeSaveSpaceService()=default;
void RuntimeSaveSpaceService::install_once(){impl_->install();}
void RuntimeSaveSpaceService::tick(std::chrono::steady_clock::time_point now){impl_->tick(now);}
}
