#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

// Only the engine boundary is faked. Both discovery and validity .cpp files
// are compiled directly from the production target.
namespace RC::Unreal
{
    class UObject;
    class UClass;
    class UFunction;
    class UScriptStruct;
    enum EObjectFlags { RF_BeginDestroyed = 1, RF_FinishDestroyed = 2,
        RF_NeedLoad = 4, RF_NeedPostLoad = 8, RF_NeedPostLoadSubobjects = 16 };
    enum EPropertyFlags { CPF_Parm = 1, CPF_OutParm = 2, CPF_ReturnParm = 4 };
    struct FUObjectItem
    {
        UObject* object{};
        bool alive{true};
        int32_t serial{1};
        auto GetUObject() const -> UObject* { return object; }
    };
    struct UObjectArray
    {
        static inline std::vector<FUObjectItem> items;
        static auto GetNumElements() -> int32_t { return static_cast<int32_t>(items.size()); }
        static auto IsValid(FUObjectItem* item, bool) -> bool { return item && item->alive; }
    };
    class UObject
    {
    public:
        int32_t index{-1};
        int flags{};
        UClass* type{};
        UObject* outer{};
        UFunction* function{};
        std::unordered_map<std::wstring, UFunction*> functions;
        std::unordered_map<std::wstring, class FProperty*> properties;
        int property_lookups{};
        int function_lookups{};
        int calls{};
        std::wstring object_name;
        std::wstring object_path;
        auto GetPathName() const -> std::wstring { return object_path; }
        int name_reads{};
        auto GetName() -> std::wstring { ++name_reads; return object_name; }
        auto GetInternalIndex() const -> int32_t { return index; }
        auto GetObjectItem() const -> const FUObjectItem* { return &UObjectArray::items[index]; }
        auto HasAnyFlags(EObjectFlags mask) -> bool { return (flags & mask) != 0; }
        auto GetClassPrivate() const -> UClass* { return type; }
        auto GetOuterPrivate() const -> UObject* { return outer; }
        auto GetFullName() const -> std::wstring { return L"test object"; }
        auto GetPropertyByNameInChain(const wchar_t* name) -> class FProperty*
        {
            ++property_lookups;
            const auto found = properties.find(name);
            return found == properties.end() ? nullptr : found->second;
        }
        auto GetFunctionByNameInChain(const wchar_t* name) -> UFunction*
        {
            ++function_lookups;
            if (functions.empty()) return function;
            const auto found = functions.find(name);
            return found == functions.end() ? nullptr : found->second;
        }
        auto ProcessEvent(UFunction*, void*) -> void;
    };
    class UStruct : public UObject
    {
    public:
        UStruct* parent{};
        auto GetSuperStruct() const -> UStruct* { return parent; }
    };
    class UClass : public UStruct
    {
    public:
        std::vector<UFunction*> hierarchy_functions;
        int function_chain_reads{};
        auto ForEachFunctionInChain() -> const std::vector<UFunction*>& { ++function_chain_reads; return hierarchy_functions; }
        static inline UClass* class_type{};
        static auto StaticClass() -> UClass* { return class_type; }
    };
    class AActor : public UObject
    {
    public:
        static inline UClass* actor_type{};
        static auto StaticClass() -> UClass* { return actor_type; }
    };
    struct PropertyClass
    {
        std::wstring name;
        mutable int name_reads{};
        auto GetName() const -> std::wstring { ++name_reads; return name; }
    };
    class FProperty
    {
    public:
        PropertyClass type;
        int32_t offset{}, size{}, flags{CPF_Parm};
        auto GetClass() const -> const PropertyClass& { return type; }
        auto GetOffset_Internal() const -> int32_t { return offset; }
        auto GetElementSize() const -> int32_t { return size; }
        auto HasAnyPropertyFlags(EPropertyFlags mask) const -> bool { return (flags & mask) != 0; }
        template<class T> auto ContainerPtrToValuePtr(void* params) const -> T*
        { return reinterpret_cast<T*>(static_cast<std::byte*>(params) + offset); }
    };
    class FArrayProperty : public FProperty
    {
    public:
        FProperty* inner{};
        auto GetInner() const -> FProperty* { return inner; }
    };
    class FBoolProperty : public FProperty
    {
    public:
        uint8_t mask{1};
        auto GetPropertyValueInContainer(void* value) const -> bool
        { return (*ContainerPtrToValuePtr<uint8_t>(value) & mask) != 0; }
        auto SetPropertyValueInContainer(void* value, bool enabled) -> void
        {
            auto* byte=ContainerPtrToValuePtr<uint8_t>(value);
            *byte=enabled ? *byte | mask : *byte & ~mask;
        }
    };
    class UScriptStruct : public UStruct
    {
    public:
        static inline UClass* struct_type{};
        std::unordered_map<std::wstring, FProperty*> fields;
        static auto StaticClass() -> UClass* { return struct_type; }
    };
    class FStructProperty : public FProperty
    {
    public:
        UScriptStruct* structure{};
        auto GetStruct() const -> UScriptStruct* { return structure; }
    };
    class UFunction : public UStruct
    {
    public:
        static inline UClass* function_type{};
        int32_t size{32};
        FProperty* context{};
        FProperty* requested_class{};
        FProperty* result{};
        int initialized{}, destroyed{};
        void* native_function{reinterpret_cast<void*>(1)};
        std::unordered_map<std::wstring, FProperty*> parameters;
        static auto StaticClass() -> UClass* { return function_type; }
        auto GetParmsSize() const -> int32_t { return size; }
        auto GetFuncPtr() const -> void* { return native_function; }
        auto InitializeStruct(void*) -> void { ++initialized; }
        auto DestroyStruct(void*) -> void { ++destroyed; }
    };
}
