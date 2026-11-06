#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/ui/title_state_ids.h"
#include "kh3ap/features/world/startup/starter_options.h"
#include "kh3ap/features/world/startup/autoload_profile.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/gfx/gfx_value_layout.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/FProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace kh3ap {
    namespace
    {
        constexpr auto max_starter_notice_characters = 1024;
        constexpr auto max_starter_notice_capacity = 65536;
    }

using namespace RC::Unreal;
namespace {
    bool write_ui_byte(UObject* object, size_t offset, uint8_t value) noexcept {
        __try {
            *(reinterpret_cast<uint8_t*>(object)+offset)=value;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool mark_pandora_notice_seen(void* owner) noexcept {
        __try {
            auto* flags=reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(owner)+0xca4);
            *flags |= 8u;
            return (*flags & 8u) != 0;
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
        template<class T> bool read(uintptr_t address,T& value) noexcept
        {
            __try { std::memcpy(&value,reinterpret_cast<void*>(address),sizeof(T)); return true; }
            __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        template<class T> T field(const void* object,size_t offset)
        {
            T value{};
            if(!object || !read(reinterpret_cast<uintptr_t>(object)+offset,value))
                throw std::runtime_error("unreadable_ui_field");
            return value;
        }
        int64_t unix_ms()
        { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
        bool signature(uintptr_t address,std::initializer_list<uint8_t> bytes)
        {
            for(auto expected:bytes) { uint8_t actual{}; if(!read(address++,actual)||actual!=expected) return false; }
            return true;
        }
        struct StartupLayout {
            bool steam;
            uint32_t timestamp, image_size;
            uintptr_t title_vtable, settings_vtable, message_vtable, menu_vtable;
            uintptr_t decide_message, decide_settings, stop_movie, confirm_new_game, select_new_game, open_difficulty;
            uintptr_t play_animation, intro_label, buttons_label, prompt_label;
            int owner_offset, movie_offset, save_state_offset, menu_shift;
        };
        constexpr StartupLayout egs_layout{
            false, kh3ap::native_build::egs.timestamp, kh3ap::native_build::egs.image_size,
            0x7c381e0, 0x7c696d8, 0x7c5b4f0, 0x7c68b78,
            0x1a4b540, 0x1ae95d0, 0x18ff240, 0x18c8ce0, 0x18c8840, 0x1aee970,
            0x3e2c8a0, 0x8d426b0, 0x8d29370, 0x8d293a0,
            0x140, 0x2b0, 0x69d0, 0};
        constexpr StartupLayout steam_layout{
            true, kh3ap::native_build::steam.timestamp, kh3ap::native_build::steam.image_size,
            0x6a56078, 0x6b6ae60, 0x6b1c838, 0x6b6b150,
            0x16c7590, 0x1752eb0, 0x15b0da0, 0x1563f70, 0x1563f90, 0x1758b90,
            0x38f9470, 0x86f9fe0, 0x86dfc00, 0x86dfc10,
            0x180, 0x2f0, 0x6a30, 8};

        const StartupLayout& startup_layout(uintptr_t base)
        {
            // Validate every mutating entry point before returning a layout.
            // Steam changes manager offsets and the title's private flag block,
            // while starter-setting fields and semantic IDs retain their layout.
            static const StartupLayout* checked = [base]() -> const StartupLayout* {
                IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
                if(!read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>=0x1000||
                   !read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE) return nullptr;
                const StartupLayout* layout=nullptr;
                for(const auto* candidate : {&egs_layout, &steam_layout})
                    if(nt.FileHeader.TimeDateStamp==candidate->timestamp && nt.OptionalHeader.SizeOfImage==candidate->image_size)
                        layout=candidate;
                if(!layout) return nullptr;
                const auto& l=*layout;
                if(!signature(base+l.confirm_new_game,{0x89,0x91,0x88,5,0,0,0xc7,0x81,0x70,5,0,0,0x14,0,0,0,0xc3}) ||
                   !signature(base+l.select_new_game,{0xc7,0x81,0x74,5,0,0,0x11,0,0,0,0xc3}) ||
                   !signature(base+l.open_difficulty,{0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x20}) ||
                   !signature(base+l.play_animation,{0x40,0x53,0x48,0x81,0xec,0xe0,0,0,0})) return nullptr;
                if(l.steam) {
                    if(!signature(base+l.decide_message,{0x48,0x8b,0xc4,0x57,0x48,0x83,0xec,0x70}) ||
                       !signature(base+l.decide_settings,{0x48,0x8b,0xc4,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x50}) ||
                       !signature(base+l.stop_movie,{0x40,0x53,0x48,0x83,0xec,0x20,0x4c,0x8b,0x81,0xf0,2,0,0}) ||
                       !signature(base+0x175e1ec,{0x48,0x8b,0x8b,0xb8,5,0,0,0xc6,0x83,0xdb,6,0,0,0,0xc6,0x83,0xe0,6,0,0,1}) ||
                       !signature(base+0x175e273,{0x48,0x8b,0x8b,0x98,5,0,0,0xc6,0x83,0xe2,6,0,0,0}) ||
                       // GI+870, system options+428, notice bits+c = GI+ca4.
                       !signature(base+0xea8620,{0x48,0x8d,0x81,0x70,8,0,0,0xc3}) ||
                       !signature(base+0x1570b3a,{0xe8,0xe1,0x7a,0x93,0xff,0x48,0x8b,0x8b,0x48,5,0,0,0x48,0x8d,0xb8,0x28,4,0,0}) ||
                       !signature(base+0xe8ccc0,{0x83,0xfa,0x1f,0x77,0x0a,0x8b,0x41,0x0c,0x0f,0xa3,0xd0,0x0f,0x92,0xc0,0xc3}) ||
                       !signature(base+0x1570c81,{0xba,3,0,0,0,0x48,0x8b,0xcf,0xe8,0x32,0xc0,0x91,0xff})) return nullptr;
                } else {
                    if(!signature(base+l.decide_message,{0x48,0x8b,0xc4,0x57,0x48,0x81,0xec,0x80,0,0,0}) ||
                       !signature(base+l.decide_settings,{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10}) ||
                       !signature(base+l.stop_movie,{0x40,0x53,0x48,0x83,0xec,0x20,0x4c,0x8b,0x81,0xb0,2,0,0}) ||
                       !signature(base+0x1aee11f,{0xc6,0x81,0xd3,6,0,0,0,0xc6,0x81,0xd8,6,0,0,1}) ||
                       !signature(base+0x1aee1e0,{0x48,0x8b,0x8b,0x98,5,0,0,0xc6,0x83,0xda,6,0,0,0}) ||
                       !signature(base+0x18c6b6e,{0x8b,0x8f,0xa4,0x0c,0,0,0x8b,0xc1,0xc1,0xe8,3}) ||
                       !signature(base+0x18c6b7d,{0x83,0xc9,8,0x89,0x8f,0xa4,0x0c,0,0})) return nullptr;
                }
                return layout;
            }();
            if(!checked) throw std::runtime_error("startup_build_mismatch");
            return *checked;
        }
        void offset(UObject* object,const wchar_t* name,int expected)
        {
            auto* p=is_uobject_valid_fast(object)?object->GetPropertyByNameInChain(name):nullptr;
            if(!p||p->GetOffset_Internal()!=expected) throw std::runtime_error("ui_reflected_layout_mismatch");
        }
}
    FastStartEligibility runtime_fast_start_eligibility(UObject* title)
    {
        if(!is_uobject_valid_fast(title)) return FastStartEligibility::waiting;
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto& layout=startup_layout(base);
        if(field<uintptr_t>(title,0)!=base+layout.title_vtable)
            throw std::runtime_error("fast_start_title_layout_mismatch");
        offset(title,L"m_UIManager",0x548);
        auto* manager=try_read_reflected_object_property_value(title,L"m_UIManager");
        if(!is_uobject_valid_fast(manager)) return FastStartEligibility::waiting;
        const auto store=layout.steam ? AutoloadStore::steam : AutoloadStore::egs;
        // Reuse the verified catalog used by the explicit native slot loader.
        // Only read its count; do not inspect any vanilla save files.
        int32_t count=-1;
        if(!read(base+autoload_rva(store,0x9c460e0),count))
            throw std::runtime_error("fast_start_catalog_unreadable");
        return fast_start_eligibility(count,field<int32_t>(title,0x570),
                                      field<int32_t>(manager,layout.save_state_offset)==0);
    }
    bool runtime_starter_options_tick(UObject* title, std::string& evidence, bool notice, StarterOptions options, bool initial_only, int pandora_message_id)
    {
        evidence="null";
        if(!is_uobject_valid_fast(title))return false;
        const auto image=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto& layout=startup_layout(image);
        offset(title,L"m_UIManager",0x548);
        if (field<uintptr_t>(title,0) != image+layout.title_vtable)
            throw std::runtime_error("initial_title_layout_mismatch");
        const auto title_state=field<int32_t>(title,0x570);
        if (initial_only && (title_state < 5 || title_state >= title_state_ids::intro_movie))
        {
            return false;
        }
        auto* manager=try_read_reflected_object_property_value(title,L"m_UIManager");
        if(title_state==8 || notice || pandora_message_id > 0)
        {
            const auto message_id=pandora_message_id > 0 ? pandora_message_id : field<int32_t>(title,0x57c);
            if(message_id<=0)return false;
            std::vector<UObject*> messages;UObjectGlobals::FindAllOf(L"TresUIP_Message",messages);
            UObject* dialog=nullptr;
            for(auto* candidate:messages)
            {
                if(!is_uobject_valid_fast(candidate)||candidate->GetPathName().find(L"Default__")!=std::wstring::npos||
                    try_read_reflected_object_property_value(candidate,L"m_pUIManager")!=manager||
                    field<int32_t>(candidate,0xa8)!=message_id)continue;
                if(dialog)throw std::runtime_error("ambiguous_starter_notice");dialog=candidate;
            }
            if(!dialog)return false;
            const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if(field<uintptr_t>(dialog,0)!=base+layout.message_vtable)
                throw std::runtime_error("starter_notice_build_mismatch");
            offset(dialog,L"m_pUIManager",0x70);
            if(try_read_reflected_object_property_value(dialog,L"m_pUIManager")!=manager)
                throw std::runtime_error("starter_notice_owner_mismatch");
            const auto text_at=[&](const void* object,size_t off){
                const auto count=field<int32_t>(object,off+8),capacity=field<int32_t>(object,off+12);
                if(count<1||count>max_starter_notice_characters||capacity<count||capacity>max_starter_notice_capacity)throw std::runtime_error("starter_notice_string_layout");
                const auto* data=field<const wchar_t*>(object,off);
                std::wstring value;for(int i=0;i<count;++i)value+=field<wchar_t>(data,i*sizeof(wchar_t));
                if(value.back()!=L'\0')throw std::runtime_error("starter_notice_string_termination");
                value.pop_back();return value;
            };
            const auto prompt=text_at(dialog,0x2a0);
            if(pandora_message_id > 0 && prompt.find(L"Pandora's Power") == std::wstring::npos)
                return false;
            const auto count=field<int32_t>(dialog,0x270);
            std::ostringstream out;out<<std::boolalpha<<"{\"phase\":\"autosave_notice\",\"prompt\":\""<<json_escape(prompt)
                <<"\",\"type\":"<<field<int32_t>(dialog,0x280)<<",\"pages\":"<<count
                <<",\"open\":"<<(field<uint8_t>(dialog,0xa1)==1)<<",\"input_ready\":"<<(field<uint8_t>(dialog,0xa2)==1)<<'}';
            evidence=out.str();
            if(!field<uint8_t>(dialog,0xa1)||!field<uint8_t>(dialog,0xa2))return false;
            if(count!=1||field<int32_t>(dialog,0x280)!=0)
                throw std::runtime_error("unexpected_starter_notice");
            alignas(16) std::array<std::byte,0x30> event{};const int32_t kind=7,index=0;
            std::memcpy(event.data()+0x10,&kind,4);std::memcpy(event.data()+0x18,&index,4);
            using Decide=bool(__fastcall*)(UObject*,void*);
            if(!reinterpret_cast<Decide>(base+layout.decide_message)(dialog,event.data())||
                field<int32_t>(dialog,0x290)!=0||!(field<uint32_t>(event.data(),0x24)&2))
                throw std::runtime_error("starter_notice_not_accepted");
            // Message handles its own completion delegate and deferred close.
            return true;
        }
        auto* settings=try_read_reflected_object_property_value(title,L"m_IniSettings");
        if(!is_uobject_valid_fast(settings))return false;
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if(field<uintptr_t>(settings,0)!=base+layout.settings_vtable)
            throw std::runtime_error("initial_settings_build_mismatch");
        offset(settings,L"m_pUIManager",0x70);
        if(try_read_reflected_object_property_value(settings,L"m_pUIManager")!=
            try_read_reflected_object_property_value(title,L"m_UIManager"))
            throw std::runtime_error("initial_settings_owner_mismatch");
        // 2cc is a pending transition operation, cleared by the native tick.
        // 2b4=3 and a2 identify the opened, input-ready presentation. The
        // requested page at 2d4 can retain an old value after a Back action.
        const auto stage=field<int32_t>(settings,0x2d0);
        std::ostringstream out;out<<std::boolalpha<<"{\"phase\":\"settings\",\"stage\":"<<stage
            <<",\"pending_stage\":"<<field<int32_t>(settings,0x2d4)
            <<",\"transition\":"<<field<int32_t>(settings,0x2cc)
            <<",\"presentation\":"<<field<int32_t>(settings,0x2b4)
            <<",\"input_ready\":"<<(field<uint8_t>(settings,0xa2)==1)
            <<",\"text\":"<<field<int32_t>(settings,0x2a8)
            <<",\"voice\":"<<field<int32_t>(settings,0x2ac)
            <<",\"keyboard\":"<<field<int32_t>(settings,0x2b0)<<'}';evidence=out.str();
        if(!field<uint8_t>(settings,0xa1)||!field<uint8_t>(settings,0xa2)||
            field<int32_t>(settings,0x2b4)!=3||field<int32_t>(settings,0x2cc)!=0)return false;
        if(stage<1||stage>4)return false;
        // These are semantic button IDs, not list indices: language ID 2 is
        // culture "en", voice ID 10 is "en", keyboard ID 13 maps to layout 0.
        if (!options.valid()) throw std::runtime_error("invalid_starter_options");
        const std::array<int32_t,5> actions{0,options.text,options.voice,options.keyboard,17};
        if(stage==4&&(field<int32_t>(settings,0x2a8)!=options.text||field<int32_t>(settings,0x2ac)!=options.voice||
            field<int32_t>(settings,0x2b0)!=options.keyboard)) throw std::runtime_error("starter_options_readback_mismatch");
        alignas(16) std::array<std::byte,0x30> event{};
        const int32_t kind=7,action=actions[stage];
        std::memcpy(event.data()+0x10,&kind,4);std::memcpy(event.data()+0x18,&action,4);
        using Decide=bool(__fastcall*)(UObject*,void*);
        if(!reinterpret_cast<Decide>(base+layout.decide_settings)(settings,event.data())||!(field<uint32_t>(event.data(),0x24)&2))
            throw std::runtime_error("starter_option_not_accepted");
        return true;
    }

    bool runtime_direct_new_game_tick(UObject* title, std::string& evidence, int difficulty,
                                      bool& dispatched, bool& movie_dismissed)
    {
        if(dispatched || !is_uobject_valid_fast(title)) return false;
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto& layout=startup_layout(base);
        if(field<uintptr_t>(title,0)!=base+layout.title_vtable)
            throw std::runtime_error("direct_new_game_build_mismatch");
        if(difficulty < -1 || difficulty > 3) throw std::runtime_error("invalid_new_game_difficulty");
        offset(title,L"m_UIManager",0x548);
        offset(title,L"m_Title",0x540);
        const int state=field<int32_t>(title,0x570);
        if (!((state >= 5 && state <= title_state_ids::menu_ready) || state == 27 || state == 53 ||
              state == 54))
        {
            return false;
        }
        auto* manager=try_read_reflected_object_property_value(title,L"m_UIManager");
        if(!is_uobject_valid_fast(manager)) return false;
        offset(manager,L"m_pGameInstance",layout.owner_offset);
        auto* owner=try_read_reflected_object_property_value(manager,L"m_pGameInstance");
        if(!is_uobject_valid_fast(owner) || !is_uobject_valid_fast(owner->GetClassPrivate()) ||
            owner->GetClassPrivate()->GetName()!=L"TresGameInstance") return false;
        // Both verified builds use GI+ca4 bit 3 for the platform keyblade
        // notice. This marks the notice seen, without changing entitlement.
        if(!mark_pandora_notice_seen(owner)) throw std::runtime_error("pandora_notice_write_failed");
        if (state != title_state_ids::intro_movie && state != 18 &&
            state != title_state_ids::menu_ready && state != 27)
        {
            return false;
        }
        if (state == title_state_ids::intro_movie && !movie_dismissed)
        {
            // UIManager's real movie stop invokes the completion delegate,
            // stops playback, clears the active movie and releases its slot.
            // Calling the title completion delegate alone leaves playback alive.
            auto* movie=field<UObject*>(manager,layout.movie_offset);
            if(!is_uobject_valid_fast(movie)) return false;
            offset(movie,L"m_pUIManager",0x70);
            if(try_read_reflected_object_property_value(movie,L"m_pUIManager")!=manager ||
               field<int32_t>(movie,0x300)!=0)
                throw std::runtime_error("title_movie_owner_mismatch");
            using StopMovie=void(__fastcall*)(UObject*);
            reinterpret_cast<StopMovie>(base+layout.stop_movie)(manager);
            if(field<UObject*>(manager,layout.movie_offset)!=nullptr || !field<uint8_t>(title,0x5af))
                throw std::runtime_error("title_movie_stop_not_observed");
            movie_dismissed=true;
            evidence="{\"phase\":\"title_movie_stopped\",\"pandora_notice_seen\":true}";
            return true;
        }
        const int message_id=field<int32_t>(title,0x5d0);
        if(state==27 && message_id>0) {
            return runtime_starter_options_tick(title,evidence,false,{},false,message_id);
        }
        if(state==18 || state==27 || message_id!=0 || field<int32_t>(manager,layout.save_state_offset)!=0) return false;
        auto* menu=try_read_reflected_object_property_value(title,L"m_Title");
        if(!is_uobject_valid_fast(menu) || field<uintptr_t>(menu,0)!=base+layout.menu_vtable ||
            field<uint8_t>(title,0x5a5)!=0 || field<int32_t>(title,0x574)==1) return false;
        if(difficulty>=0) {
            dispatched=true; // Never retry a potentially completed New Game.
            using Confirm=void(__fastcall*)(UObject*,int32_t);
            reinterpret_cast<Confirm>(base+layout.confirm_new_game)(title,difficulty);
            if(field<int32_t>(title,0x570)!=20 || field<int32_t>(title,0x588)!=difficulty)
                throw std::runtime_error("new_game_not_observed");
            evidence="{\"phase\":\"new_game_started\",\"difficulty\":"+std::to_string(difficulty)+"}";
            return true;
        }
        if (state == title_state_ids::intro_movie)
        {
            return false;
        }
        // TresUIP_Title's native input handler (1aee0c0) has two title-only
        // branches before ordinary menu input: skip the intro animation, then
        // dismiss the press-button presentation. Use those same animation
        // transitions without changing the shared controller/input state.
        if(field<uint8_t>(menu,0xa1) &&
           (field<uint8_t>(menu,0x6da+layout.menu_shift) || field<uint8_t>(menu,0x6d3+layout.menu_shift))) {
            const auto play=[&](size_t property, uintptr_t label) {
                auto* animation=field<void*>(menu,property);
                if (!animation || (field<uint32_t>(animation, gfx_native_value::type_offset) &
                                   gfx_native_value::value_type_mask) !=
                                      gfx_native_value::display_object_value_type)
                {
                    throw std::runtime_error("title_animation_not_ready");
                }
                using Play=void(__fastcall*)(void*,const void*);
                reinterpret_cast<Play>(base+layout.play_animation)(animation,reinterpret_cast<const void*>(base+label));
            };
            if(field<uint8_t>(menu,0x6da+layout.menu_shift)) {
                play(0x598,layout.intro_label);
                if(!write_ui_byte(menu,0x6da+layout.menu_shift,0)) throw std::runtime_error("title_intro_skip_failed");
                evidence="{\"phase\":\"title_intro_skipped\"}";
            } else {
                play(0x5b8,layout.buttons_label);
                play(0x608,layout.prompt_label);
                if(!write_ui_byte(menu,0x6d3+layout.menu_shift,0) || !write_ui_byte(menu,0x6d8+layout.menu_shift,1))
                    throw std::runtime_error("title_prompt_skip_failed");
                evidence="{\"phase\":\"title_prompt_skipped\"}";
            }
            // The native animation completion still owns input readiness.
            return true;
        }
        if(!field<uint8_t>(menu,0xa2) || field<uint8_t>(menu,0x6d1+layout.menu_shift) ||
            field<uint8_t>(menu,0x6d2+layout.menu_shift) || field<uint8_t>(menu,0x6d0+layout.menu_shift) || field<uint8_t>(menu,0x6d3+layout.menu_shift)) return false;
        // Normal New Game-selected delegate, then the normal fresh difficulty UI.
        using Select=void(__fastcall*)(UObject*);
        using Open=void(__fastcall*)(UObject*,bool);
        dispatched=true;
        reinterpret_cast<Select>(base+layout.select_new_game)(title);
        reinterpret_cast<Open>(base+layout.open_difficulty)(menu,false);
        if(!field<uint8_t>(menu,0x6d1+layout.menu_shift)) throw std::runtime_error("new_game_menu_not_opened");
        evidence="{\"phase\":\"difficulty_selection_opened\"}";
        return true;
    }

}
