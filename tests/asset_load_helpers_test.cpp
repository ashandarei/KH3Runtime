#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/Runtime.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Property/FSoftObjectProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <cassert>
#include <cstring>
#include <new>

using namespace RC::Unreal;
using namespace kh3ap;
namespace {
UObject* library{}; UObject* loaded{}; UObject* expected_context{};
UFunction* conversion{}; UFunction* load_function{};
int global_lookups{}, param_lookups{}, initialized{}, destroyed{};
bool init_fails{};
std::wstring observed_path;
int32_t observed_uuid{};
auto add(UObject& object,UClass* type,UObject* outer=nullptr) -> void {
    object.type=type; object.outer=outer; object.index=UObjectArray::GetNumElements();
    UObjectArray::items.push_back({&object});
}
}
auto RC::Unreal::UObjectGlobals::find(const wchar_t* path) -> UObject* {
    assert(std::wstring_view{path}==L"/Script/Engine.Default__KismetSystemLibrary");
    ++global_lookups; return library;
}
auto kh3ap::make_uobject_weak(UObject* object) -> FWeakObjectPtr {
    FWeakObjectPtr weak; if (is_uobject_valid_fast(object)) weak=object; return weak;
}
auto kh3ap::find_function_param_by_name(UFunction* fn,std::wstring_view name) -> FProperty* {
    ++param_lookups;
    auto found=fn->parameters.find(std::wstring{name});
    return found==fn->parameters.end()?nullptr:found->second;
}
auto kh3ap::initialize_property_value_dynamic(FProperty* property,void* value) -> bool {
    if (init_fails) return false;
    ++initialized;
    if (property->GetClass().GetName().starts_with(L"Soft")) new (value) FSoftObjectPtr{};
    else new (static_cast<std::byte*>(value)+16) FString{};
    return true;
}
auto kh3ap::destroy_property_value_dynamic(FProperty* property,void* value) -> void {
    ++destroyed;
    if (property->GetClass().GetName().starts_with(L"Soft")) static_cast<FSoftObjectPtr*>(value)->~FSoftObjectPtr();
    else reinterpret_cast<FString*>(static_cast<std::byte*>(value)+16)->~FString();
}
auto UObject::ProcessEvent(UFunction* fn,void* params) -> void {
    ++calls;
    auto* soft=fn->parameters.at(fn->parameters.contains(L"AssetClass")?L"AssetClass":L"Asset");
    auto* value=soft->ContainerPtrToValuePtr<std::byte>(params);
    observed_path=soft->GetClass().GetName().starts_with(L"Soft")
        ? reinterpret_cast<FSoftObjectPtr*>(value)->ObjectID.AssetPathName.value
        : reinterpret_cast<FString*>(value+16)->data;
    if (fn==conversion) {
        std::memcpy(fn->parameters.at(L"ReturnValue")->ContainerPtrToValuePtr<void>(params),&loaded,sizeof(loaded));
    } else {
        assert(fn==load_function);
        assert(*fn->parameters.at(L"WorldContextObject")->ContainerPtrToValuePtr<UObject*>(params)==expected_context);
        auto* info=fn->parameters.at(L"LatentInfo")->ContainerPtrToValuePtr<std::byte>(params);
        std::memcpy(&observed_uuid,info+4,4);
        UObject* callback{}; std::memcpy(&callback,info+16,8);
        assert(callback==expected_context);
    }
}
int main() {
    UClass meta,root,library_type,function_type;
    UClass::class_type=&meta; UFunction::function_type=&function_type;
    for (auto* type:{&meta,&root,&library_type,&function_type}) add(*type,&meta);
    library_type.parent=function_type.parent=&root;
    UObject owner,context; add(owner,&library_type); add(context,&root);
    library=&owner; expected_context=&context; loaded=&library_type;
    UFunction conv,request;
    add(conv,&function_type,&library_type); add(request,&function_type,&library_type);
    conversion=&conv; load_function=&request;
    FProperty conv_soft{{L"AssetClassProperty"},0,32}, returned{{L"ClassProperty"},32,8,CPF_Parm|CPF_ReturnParm};
    FProperty context_param{{L"ObjectProperty"},0,8}, load_soft{{L"AssetClassProperty"},8,32};
    FProperty latent{{L"StructProperty"},40,24};
    conv.size=40; request.size=64;
    conv.parameters={{L"AssetClass",&conv_soft},{L"ReturnValue",&returned}};
    request.parameters={{L"WorldContextObject",&context_param},{L"AssetClass",&load_soft},{L"LatentInfo",&latent}};
    owner.functions={{L"Conv_SoftClassReferenceToClass",&conv},{L"LoadAssetClass",&request}};
    assert(find_loaded_runtime_asset_class(L"/Game/One.One_C")==&library_type);
    const int lookups=param_lookups;
    for (int i=0;i<50;++i) assert(find_loaded_runtime_asset_class(L"/Game/Two.Two_C")==&library_type);
    assert(global_lookups==1 && owner.function_lookups==1 && param_lookups==lookups);
    assert(observed_path==L"/Game/Two.Two_C" && initialized==destroyed);
    loaded=nullptr; assert(!find_loaded_runtime_asset_class(L"/Game/Missing.Missing_C"));
    loaded=&library_type; library_type.flags=RF_NeedPostLoad;
    assert(!find_loaded_runtime_asset_class(L"/Game/Pending.Pending_C")); library_type.flags=0;
    std::wstring resolution;
    assert(request_runtime_asset_class_load(&context,L"/Game/Cold.Cold_C",&resolution,12345));
    assert(resolution==L"kismet_load_asset_class_requested" && observed_uuid==12345);
    assert(observed_path==L"/Game/Cold.Cold_C" && initialized==destroyed);
    const int before=destroyed;
    ++UObjectArray::items[request.index].serial;
    latent.size=4;
    assert(!request_runtime_asset_class_load(&context,L"/Game/Bad.Bad_C",&resolution));
    assert(destroyed==before+1 && initialized==destroyed);
    latent.size=24;
    init_fails=true;
    assert(!request_runtime_asset_class_load(&context,L"/Game/Bad.Bad_C",&resolution));
    assert(initialized==destroyed); init_fails=false;
    ++UObjectArray::items[conv.index].serial;
    returned.offset=39;
    assert(!find_loaded_runtime_asset_class(L"/Game/Bad.Bad_C"));
    returned.offset=32;
    assert(find_loaded_runtime_asset_class(L"/Game/Recovered.Recovered_C")==&library_type);
    ++UObjectArray::items[conv.index].serial;
    conv_soft.type.name=L"SoftClassProperty";
    assert(find_loaded_runtime_asset_class(L"/Game/Modern.Modern_C")==&library_type);
    assert(observed_path==L"/Game/Modern.Modern_C" && initialized==destroyed);
    conv.size=300;
    assert(!find_loaded_runtime_asset_class(L"/Game/Oversized.Oversized_C"));
    conv.size=40;
    ++UObjectArray::items[owner.index].serial;
    assert(find_loaded_runtime_asset_class(L"/Game/Replacement.Replacement_C")==&library_type);
    assert(global_lookups==2);
    assert(!request_runtime_asset_class_load(nullptr,L"/Game/NoContext",&resolution));
    // The object path shares scratch ownership but has distinct function and
    // reflected parameter names. Returned asset values must remain live.
    ++UObjectArray::items[conv.index].serial;
    ++UObjectArray::items[request.index].serial;
    conv_soft.type.name=L"AssetObjectProperty";
    load_soft.type.name=L"AssetObjectProperty";
    returned.type.name=L"ObjectProperty";
    conv.parameters.erase(L"AssetClass"); conv.parameters[L"Asset"]=&conv_soft;
    request.parameters.erase(L"AssetClass"); request.parameters[L"Asset"]=&load_soft;
    owner.functions[L"Conv_SoftObjectReferenceToObject"]=&conv;
    owner.functions[L"LoadAsset"]=&request;
    loaded=&context;
    assert(find_loaded_runtime_asset(L"/Game/Object.Object")==&context);
    assert(request_runtime_asset_load(&context,L"/Game/Async.Async",&resolution,24680));
    assert(observed_uuid==24680 && observed_path==L"/Game/Async.Async");
    assert(resolution==L"kismet_load_asset_requested" && initialized==destroyed);
}
