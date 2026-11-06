#include "kh3ap/features/world/space/space_landing.h"
#include "kh3ap/features/world/space/space_profile.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/NameTypes.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace kh3ap {
    namespace
    {
        constexpr int maximum_landing_array_capacity = 4096;
        constexpr int maximum_landing_parameter_bytes = 4096;
        constexpr int maximum_landing_cache_entries = 512;

        constexpr auto landing_asset_retry_interval = std::chrono::seconds(2);
    }

using namespace RC::Unreal;
namespace {
constexpr auto table_path=L"/Game/Blueprints/GoA/Data/KH3AreaSelectData_DT.KH3AreaSelectData_DT";
constexpr auto evaluator_path=L"/Game/Blueprints/GoA/UI/BP_KH3AreaSelectLibrary.BP_KH3AreaSelectLibrary_C";
constexpr auto portal_path=L"/Game/Maps/GardenOfAssemblage/uasset/Textures/Portals/Portal_Base.Portal_Base_C";
struct Array { void* data{}; int32_t num{},max{}; };
template<class T> void assign(Array& array,const std::vector<T>& values) {
    if (array.num < 0 || array.max < array.num || array.max > maximum_landing_array_capacity)
    {
        throw std::runtime_error("landing_array_layout");
    }
    if(array.max<static_cast<int32_t>(values.size())) {
        auto* memory=ue_realloc_for_script_array(array.data,values.size()*sizeof(T),alignof(T));
        if(!memory) throw std::runtime_error("landing_array_allocation");
        array.data=memory;array.max=static_cast<int32_t>(values.size());
    }
    if(!values.empty()) std::memcpy(array.data,values.data(),values.size()*sizeof(T));
    array.num=static_cast<int32_t>(values.size());
}
struct Call {
    UObject* owner; UFunction* fn; std::vector<std::byte> data;
    Call(UObject* o,const wchar_t* name):owner(o),fn(is_uobject_valid_fast(o)?o->GetFunctionByNameInChain(name):nullptr) {
        const auto label=to_utf8(name);
        if(!is_uobject_valid_fast(o))throw std::runtime_error("landing_owner_missing:"+label);
        if(!is_uobject_valid_fast(fn))throw std::runtime_error("landing_function_missing:"+label);
        if (fn->GetParmsSize() <= 0 || fn->GetParmsSize() > maximum_landing_parameter_bytes)
        {
            throw std::runtime_error("landing_parameter_size:" + label);
        }
        data.resize(fn->GetParmsSize());
    }
    FProperty* field(const wchar_t* name) {
        auto* p=find_function_param_by_name(fn,name);
        if(!p || p->GetOffset_Internal()<0 || p->GetElementSize()<=0 || p->GetOffset_Internal()+p->GetElementSize()>data.size()) throw std::runtime_error("landing_parameter_layout");
        return p;
    }
    void run(){const RuntimeGrantInternalCallDepth::Scope scope{};owner->ProcessEvent(fn,data.data());}
    bool boolean(const wchar_t* name){auto* p=field(name);if(!p->IsA<FBoolProperty>())throw std::runtime_error("landing_boolean_layout");return static_cast<FBoolProperty*>(p)->GetPropertyValueInContainer(data.data());}
};
UObject* loaded(const wchar_t* path){return find_loaded_runtime_asset(path);}
UObject* cdo(const wchar_t* path){auto* c=find_loaded_runtime_asset_class(path);return c?c->GetClassDefaultObject():nullptr;}
std::wstring upper(std::wstring_view code){std::wstring s(code);for(auto& c:s)c=static_cast<wchar_t>(std::towupper(c));return s;}
FWeakObjectPtr weak(UObject* object) {
    // UE4SS assignment only copies an existing serial. Newly loaded Blueprint
    // CDOs can still have serial zero; the native assignment allocates it.
    FWeakObjectPtr result;
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    using space::native_profile;
    if(std::memcmp(reinterpret_cast<void*>(base+native_profile().weak_assign_rva),native_profile().weak_assign_bytes.data(),native_profile().weak_assign_bytes.size()))throw std::runtime_error("landing_weak_signature");
    reinterpret_cast<void(*)(FWeakObjectPtr*,UObject*)>(base+native_profile().weak_assign_rva)(&result,object);
    if(object && result.Get()!=object)throw std::runtime_error("landing_weak_assignment");
    return result;
}
}
struct RuntimeSpaceLanding::Impl {
    FWeakObjectPtr asset,original_table,table,evaluator,portal,context;
    bool swapped{},ready{};
    uint64_t builds{}, progress_evaluations{};
    std::wstring error;
    std::chrono::steady_clock::time_point next_load{};
    std::vector<FName> row_names;
    std::vector<const void*> rows;
    std::vector<int32_t> selected;
    int32_t selected_world{-1};
    void rebuild(UObject* object) {
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        reinterpret_cast<void(*)(UObject*)>(base+space::native_profile().save_menu_rebuild_rva)(object);
    }
    void restore() {
        auto* a=asset.Get();auto* original=original_table.Get();
        if(swapped && is_uobject_valid_fast(a) && is_uobject_valid_fast(original)) {
            *reinterpret_cast<UObject**>(reinterpret_cast<std::byte*>(a)+0x50)=original;
            rebuild(a);
        }
        swapped=false;ready=false;selected.clear();selected_world=-1;
    }
    bool prepare(UObject* owner) {
        auto* t=loaded(table_path);auto* e=cdo(evaluator_path);auto* p=cdo(portal_path);
        if(!t || !e || !p) {
            const auto now=std::chrono::steady_clock::now();
            if(now>=next_load){
                if(!t)request_runtime_asset_load(owner,table_path,nullptr,0x4b483481);
                if(!e)request_runtime_asset_class_load(owner,evaluator_path,nullptr,0x4b483482);
                if(!p)request_runtime_asset_class_load(owner,portal_path,nullptr,0x4b483483);
                next_load=now+landing_asset_retry_interval;
            }
            return false;
        }
        auto* a=loaded(L"/Game/UI/Data/SaveMenuDataAsset.SaveMenuDataAsset");
        if(!a)return false;
        auto* property=a->GetPropertyByNameInChain(L"AreaSelectData");
        auto* structure=t->GetPropertyByNameInChain(L"RowStruct");
        if(!property || property->GetOffset_Internal()!=0x50 || property->GetElementSize()!=8 ||
           !structure || structure->GetOffset_Internal()!=0x40)throw std::runtime_error("landing_asset_layout");
        auto* original=*property->ContainerPtrToValuePtr<UObject*>(a);
        if(!is_uobject_valid_fast(original))throw std::runtime_error("landing_original_table_missing");
        // Keep the bounded set of shared cooked assets alive, including the
        // original table while its reflected reference is temporarily replaced.
        a->SetRootSet();original->SetRootSet();t->SetRootSet();e->SetRootSet();p->SetRootSet();e->GetClassPrivate()->SetRootSet();p->GetClassPrivate()->SetRootSet();
        Call policy(e,L"EvaluateAreaSelectRow");
        asset=weak(a);original_table=weak(original);table=weak(t);evaluator=weak(e);portal=weak(p);
        *property->ContainerPtrToValuePtr<UObject*>(a)=t;swapped=true;
        rebuild(a); // Native PostLoad owns both the row array and name->index map.
        const auto& cache=*reinterpret_cast<Array*>(reinterpret_cast<std::byte*>(a)+0x60);
        if (cache.num <= 0 || cache.num > maximum_landing_cache_entries || cache.max < cache.num ||
            !cache.data)
        {
            throw std::runtime_error("landing_cache_layout");
        }
        rows.assign(static_cast<const void**>(cache.data),static_cast<const void**>(cache.data)+cache.num);
        Call names(loaded(L"/Script/Engine.Default__DataTableFunctionLibrary"),L"GetDataTableRowNames");
        if(!write_object_function_param(names.fn,names.data.data(),L"Table",t))throw std::runtime_error("landing_table_parameter");
        names.run();auto* out=names.field(L"OutRowNames");auto& values=*out->ContainerPtrToValuePtr<Array>(names.data.data());
        if(values.num!=cache.num){destroy_property_value_dynamic(out,&values);throw std::runtime_error("landing_row_count");}
        row_names.assign(static_cast<FName*>(values.data),static_cast<FName*>(values.data)+values.num);
        destroy_property_value_dynamic(out,&values);
        // The native cache is built in the table's GetRowNames order.
        struct Entry { FName name; const void* row; uint64_t hash; };
        const auto& entries=*reinterpret_cast<Array*>(reinterpret_cast<std::byte*>(t)+0x48);
        if(entries.num!=cache.num || !entries.data)throw std::runtime_error("landing_sparse_table");
        for(int i=0;i<entries.num;++i){const auto& entry=static_cast<Entry*>(entries.data)[i];if(entry.name!=row_names[i] || entry.row!=rows[i])throw std::runtime_error("landing_cache_row_order");}
        ready=true;error.clear();return true;
    }
    std::optional<bool> progressed(std::wstring_view code) {
        if(!ready || !is_uobject_valid_fast(portal.Get()))return std::nullopt;
        ++progress_evaluations;
        const auto flag=L"gameflow_"+upper(code);
        Call get(loaded(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),L"GetGameFlag");
        if(!write_name_or_string_function_param(get.field(L"FlagName"),get.data.data(),flag))throw std::runtime_error("landing_flag_parameter");
        get.run();auto* value=get.field(L"ReturnValue");
        if(value->GetElementSize()!=4)throw std::runtime_error("landing_flag_result");
        Call progress_call(portal.Get(),L"HasWorldProgressed");
        if(!write_name_or_string_function_param(progress_call.field(L"FlagName"),progress_call.data.data(),flag) ||
           !write_integral_function_param(progress_call.field(L"FlagValue"),progress_call.data.data(),*value->ContainerPtrToValuePtr<int32_t>(get.data.data())))throw std::runtime_error("landing_progress_parameters");
        progress_call.run();return progress_call.boolean(L"HasWorldStarted");
    }
    std::vector<int32_t> choices(int32_t world_code) {
        std::wstring prefix;
        for(const auto& world:space::landings)if(world.world_code==world_code)prefix=upper(world.code);
        if(prefix.empty())return {};
        Call evaluate(evaluator.Get(),L"EvaluateAreaSelectRow");
        auto* row=evaluate.field(L"Row");
        if(!row->IsA<FStructProperty>() || row->GetElementSize()!=0xd8)throw std::runtime_error("landing_row_layout");
        write_object_function_param(evaluate.fn,evaluate.data.data(),L"__WorldContext",context.Get());
        std::vector<int32_t> result;
        for(size_t i=0;i<rows.size();++i){
            const auto name=row_names[i].ToString();
            if(!name.starts_with(prefix+L"_SAVE") && !name.starts_with(prefix+L"_BT_SAVE"))continue;
            // Keep the temporary Frozen Castle save point in the Garden
            // selector only. The native landing picker must never offer it,
            // even while the shared Garden policy considers it available.
            if(name==L"FZ_SAVE08")continue;
            if(!rows[i] || *(static_cast<const uint8_t*>(rows[i])+8)!=world_code)continue;
            // Borrow the immutable input row only for ProcessEvent. Do not
            // destroy its FString/array fields as if they were owned parameters.
            std::memcpy(row->ContainerPtrToValuePtr<void>(evaluate.data.data()),rows[i],row->GetElementSize());
            evaluate.run();
            if(evaluate.boolean(L"AvailableByProgress") && (evaluate.boolean(L"ForceOpenReached") || !evaluate.boolean(L"HasForceOpenRule")))result.push_back(static_cast<int32_t>(i));
        }
        std::stable_sort(result.begin(),result.end(),[&](int a,int b){return *reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(rows[a])+0x60)<*reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(rows[b])+0x60);});
        return result;
    }
};
RuntimeSpaceLanding::RuntimeSpaceLanding():impl(std::make_unique<Impl>()){}
RuntimeSpaceLanding::~RuntimeSpaceLanding(){impl->restore();}
bool RuntimeSpaceLanding::ready() const { return impl->ready; }
bool RuntimeSpaceLanding::tick(UObject* context,bool in_space){
    if(!in_space){if(impl->swapped)impl->restore();return false;}
    if(!is_uobject_valid_fast(context))return false;
    try{
        if(impl->context.Get()!=context)impl->context=weak(context);
        if(!impl->ready)return impl->prepare(context);
    }catch(const std::exception& e){impl->restore();impl->error=std::wstring(e.what(),e.what()+std::strlen(e.what()));}
    return false;
}
std::optional<bool> RuntimeSpaceLanding::progressed(std::wstring_view code){
    try{return impl->progressed(code);}catch(const std::exception& e){impl->error=std::wstring(e.what(),e.what()+std::strlen(e.what()));return std::nullopt;}
}
bool RuntimeSpaceLanding::build_list(UObject* menu,int32_t world_code){
    if(!is_uobject_valid_fast(menu))return false;
    try{
        // A menu request queued before asset preparation must not keep the
        // original builder's retail destinations when Garden data is missing.
        if(!impl->ready)throw std::runtime_error("landing_garden_data_not_ready");
        auto choices=impl->choices(world_code);
        auto* bytes=reinterpret_cast<std::byte*>(menu);
        assign(*reinterpret_cast<Array*>(bytes+0x450),choices);
        assign<int32_t>(*reinterpret_cast<Array*>(bytes+0x468),{});
        impl->selected=std::move(choices);impl->selected_world=world_code;++impl->builds;impl->error.clear();return true;
    }catch(const std::exception& e){
        // Never offer retail-only choices when the Garden policy failed.
        reinterpret_cast<Array*>(reinterpret_cast<std::byte*>(menu)+0x450)->num=0;
        reinterpret_cast<Array*>(reinterpret_cast<std::byte*>(menu)+0x468)->num=0;
        impl->selected.clear();impl->selected_world=world_code;
        impl->error=std::wstring(e.what(),e.what()+std::strlen(e.what()));return false;
    }
}
void RuntimeSpaceLanding::write_status(std::ostream& out){
    out<<"{\"ready\":"<<(impl->ready?"true":"false")<<",\"cache_swapped\":"<<(impl->swapped?"true":"false")<<",\"builds\":"<<impl->builds<<",\"progress_evaluations\":"<<impl->progress_evaluations<<",\"error\":\""<<json_escape(impl->error)<<"\",\"world_code\":"<<impl->selected_world<<",\"rows\":[";
    bool first=true;for(auto i:impl->selected){if(!first)out<<',';first=false;out<<"{\"row\":\""<<json_escape(impl->row_names[i].ToString())<<"\",\"tag\":\""<<json_escape(reinterpret_cast<const FName*>(static_cast<const uint8_t*>(impl->rows[i])+0x10)->ToString())<<"\"}";}out<<"]}";
}
}
