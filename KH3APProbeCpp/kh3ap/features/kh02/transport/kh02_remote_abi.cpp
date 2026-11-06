// The file adapter uses matching licensed UE4.17 Core headers to preserve its
// container and callback ABI in both player and diagnostics builds.
#include "kh02_async_reads.h"
#include "Templates/Function.h"
#include "HAL/UnrealMemory.h"
// Select this old header's supported decltype(auto) implementation on modern
// MSVC. Its legacy dependent trailing-return syntax does not parse in VS2022.
#undef USING_CODE_ANALYSIS
#define USING_CODE_ANALYSIS 1
#include "Templates/Tuple.h"
#undef USING_CODE_ANALYSIS
#define USING_CODE_ANALYSIS 0
#include "Containers/Map.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstddef>

extern "C" bool kh02_remote_read(unsigned int file, unsigned long long offset, unsigned int length, void* destination);
extern "C" long long kh02_remote_file_size(unsigned int file);
extern "C" void* kh02_unreal_malloc(size_t count, unsigned int alignment);
extern "C" void* kh02_unreal_realloc(void* data, size_t count, unsigned int alignment);
extern "C" void kh02_unreal_free(void* data);
extern "C" size_t kh02_unreal_quantize(size_t count, unsigned int alignment);
extern "C" void kh02_async_event(unsigned int kind, unsigned long long first, unsigned long long second);

void* FMemory::Malloc(SIZE_T count, uint32 alignment)
{
    return kh02_unreal_malloc(count, alignment);
}

void* FMemory::Realloc(void* data, SIZE_T count, uint32 alignment)
{
    return kh02_unreal_realloc(data, count, alignment);
}

void FMemory::Free(void* data)
{
    kh02_unreal_free(data);
}

SIZE_T FMemory::QuantizeSize(SIZE_T count, uint32 alignment)
{
    return kh02_unreal_quantize(count, alignment);
}

void FGenericPlatformMemory::MemswapGreaterThan8(void* first, void* second, SIZE_T count)
{
    auto* a = static_cast<unsigned char*>(first);
    auto* b = static_cast<unsigned char*>(second);
    for (SIZE_T i = 0; i < count; ++i)
    {
        std::swap(a[i], b[i]);
    }
}

namespace
{
    // Non-case-preserving FName has this trivial key representation. Its native
    // hash is ComparisonIndex+Number. Use the matching UE4 container implementation
    // for insertion rather than treating a sparse TMap as a flat array.
    // Shipping FName's comparison union is 8-byte aligned. This matters for
    // TMap<FName,int32>: a set element is 24 bytes, with HashNextId at +0x10.
    // The pointer-valued maps happened to have the same stride without this.
    struct alignas(8) FunctionNameKey
    {
        uint32 comparison, number;

        bool operator==(const FunctionNameKey& other) const
        {
            return comparison == other.comparison && number == other.number;
        }
    };

    uint32 GetTypeHash(const FunctionNameKey& key)
    {
        return key.comparison + key.number;
    }

    using FunctionMap = TMap<FunctionNameKey, void*>;
    static_assert(sizeof(FunctionMap) == 80 && sizeof(FunctionNameKey) == 8);
} // namespace

extern "C" bool kh02_function_map_add(void* address, unsigned long long anchor_name, void* anchor,
                                      unsigned long long name, void* function)
{
    auto* map = static_cast<FunctionMap*>(address);
    FunctionNameKey old_key, new_key;
    std::memcpy(&old_key, &anchor_name, 8);
    std::memcpy(&new_key, &name, 8);
    if (map->Num() < 1 || map->Num() > 4096)
    {
        return false;
    }
    auto* old = map->Find(old_key);
    if (!old || *old != anchor)
    {
        return false;
    }
    for (const auto& entry : *map)
    {
        auto* value = map->Find(entry.Key);
        if (!value || *value != entry.Value)
        {
            return false;
        }
    }
    if (auto* current = map->Find(new_key))
    {
        return *current == function;
    }
    const auto before = map->Num();
    map->Add(new_key, function);
    auto* current = map->Find(new_key);
    if (map->Num() != before + 1 || !current || *current != function)
    {
        return false;
    }
    for (const auto& entry : *map)
    {
        auto* value = map->Find(entry.Key);
        if (!value || *value != entry.Value)
        {
            return false;
        }
    }
    return true;
}

extern "C" void* kh02_table_row_find(void* address, unsigned long long name)
{
    auto* map = static_cast<FunctionMap*>(address);
    FunctionNameKey key;
    std::memcpy(&key, &name, 8);
    if (map->Num() < 0 || map->Num() > 4096)
    {
        return nullptr;
    }
    auto* value = map->Find(key);
    return value ? *value : nullptr;
}

extern "C" bool kh02_table_rows_visit(void* address, void (*visit)(unsigned long long, void*, void*), void* opaque)
{
    auto* map = static_cast<FunctionMap*>(address);
    // Match the find/add bound. KH3's shared Sora attack table has more
    // than 256 rows; each entry is still validated before visiting any row.
    if (map->Num() < 1 || map->Num() > 4096 || !visit)
    {
        return false;
    }
    for (const auto& entry : *map)
    {
        auto* value = map->Find(entry.Key);
        if (!value || *value != entry.Value || !entry.Value)
        {
            return false;
        }
    }
    for (const auto& entry : *map)
    {
        unsigned long long name;
        std::memcpy(&name, &entry.Key, 8);
        visit(name, entry.Value, opaque);
    }
    return true;
}

extern "C" bool kh02_table_row_add(void* address, unsigned long long name, void* row)
{
    auto* map = static_cast<FunctionMap*>(address);
    FunctionNameKey key;
    std::memcpy(&key, &name, 8);
    if (map->Num() < 0 || map->Num() > 4096 || map->Find(key))
    {
        return false;
    }
    for (const auto& entry : *map)
    {
        auto* value = map->Find(entry.Key);
        if (!value || *value != entry.Value)
        {
            return false;
        }
    }
    const auto before = map->Num();
    map->Add(key, row);
    auto* value = map->Find(key);
    return map->Num() == before + 1 && value && *value == row;
}

extern "C" bool kh02_treasure_index_bind(void* address, unsigned long long name, int index)
{
    using IndexMap = TMap<FunctionNameKey, int32>;
    static_assert(sizeof(IndexMap) == 80);
    static_assert(sizeof(TSetElement<TPair<FunctionNameKey, int32>>) == 24);
    auto* map = static_cast<IndexMap*>(address);
    FunctionNameKey key;
    std::memcpy(&key, &name, 8);
    // Forty imported chest identities use the journal tail. Every insertion
    // below still rejects an occupied index or an existing name with another bit.
    if (map->Num() < 1 || map->Num() > 4096 || index < 4056 || index > 4095)
    {
        return false;
    }
    for (const auto& entry : *map)
    {
        auto* value = map->Find(entry.Key);
        if (!value || *value != entry.Value || entry.Value < 0 || entry.Value > 4095)
        {
            return false;
        }
        if (entry.Value == index && !(entry.Key == key))
        {
            return false;
        }
    }
    if (auto* existing = map->Find(key))
    {
        return *existing == index;
    }
    const auto before = map->Num();
    map->Add(key, index);
    auto* value = map->Find(key);
    return map->Num() == before + 1 && value && *value == index;
}

namespace
{
    // UE4.17's interface ABI, with engine statistics bookkeeping omitted. Only the
    // virtual interface and request storage cross into the game. Callback storage
    // uses the matching UE4 TFunction implementation, not std::function.
    class IFileHandle
    {
      public:
        virtual ~IFileHandle() = default;
        virtual int64 Tell() = 0;
        virtual bool Seek(int64) = 0;
        virtual bool SeekFromEnd(int64) = 0;
        virtual bool Read(uint8*, int64) = 0;
        virtual bool Write(const uint8*, int64) = 0;

        virtual void Flush() {}

        virtual int64 Size() = 0;
    };
    class IAsyncReadRequest;
    using FAsyncFileCallBack = TFunction<void(bool, IAsyncReadRequest*)>;

    enum EAsyncIOPriority
    {
        AIOP_Precache = 0
    };

    class IAsyncReadRequest
    {
      protected:
        // This shipping KH3 build places the result at +0x10, callback at +0x20,
        // and completion flags at +0x70 (verified native SizeRequest code).
        union
        {
            PTRINT Size;
            uint8* Memory;
        };

        FAsyncFileCallBack Callback;
        bool bDataIsReady{}, bCompleteAndCallbackCalled{}, bCompleteSync{}, bCanceled{};
        const bool bSizeRequest, bUserSuppliedMemory;

        IAsyncReadRequest(FAsyncFileCallBack* cb, bool size, uint8* supplied)
            : bSizeRequest(size), bUserSuppliedMemory(supplied != nullptr)
        {
            if (cb)
            {
                Callback = *cb;
            }
            if (size)
            {
                Size = -1;
            }
            else
            {
                Memory = supplied;
            }
        }

        void SetComplete()
        {
            bDataIsReady = true;
            std::atomic_thread_fence(std::memory_order_seq_cst);
            kh02_async_event(2, !!Callback, bCanceled);
            if (Callback)
            {
                try
                {
                    Callback(bCanceled, this);
                }
                catch (...)
                {
                    bCanceled = true;
                    kh02_async_event(4, 0, 0);
                }
            }
            kh02_async_event(3, 0, 0);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            bCompleteAndCallbackCalled = true;
            std::atomic_thread_fence(std::memory_order_seq_cst);
        }

      public:
        virtual ~IAsyncReadRequest() = default;

        static constexpr size_t ResultOffset()
        {
            return offsetof(IAsyncReadRequest, Size);
        }

        static constexpr size_t CallbackOffset()
        {
            return offsetof(IAsyncReadRequest, Callback);
        }

        static constexpr size_t FlagsOffset()
        {
            return offsetof(IAsyncReadRequest, bDataIsReady);
        }

      protected:
        virtual void WaitCompletionImpl(float) = 0;
        virtual void CancelImpl() = 0;
    };

    class IAsyncReadFileHandle
    {
      public:
        virtual ~IAsyncReadFileHandle() = default;
        virtual IAsyncReadRequest* SizeRequest(FAsyncFileCallBack*) = 0;
        virtual IAsyncReadRequest* ReadRequest(int64, int64, EAsyncIOPriority, FAsyncFileCallBack*, uint8*) = 0;
    };

    static_assert(sizeof(FAsyncFileCallBack) == 0x50);
    static_assert(sizeof(IAsyncReadRequest) == 0x80);
    static_assert(IAsyncReadRequest::ResultOffset() == 0x10, "result_offset");
    static_assert(IAsyncReadRequest::CallbackOffset() == 0x20, "callback_offset");
    static_assert(IAsyncReadRequest::FlagsOffset() == 0x70, "flags_offset");

    class SyncHandle final : public IFileHandle
    {
        uint32 file_;
        int64 offset_{};

        int64 file_size() const
        {
            return kh02_remote_file_size(file_);
        }

      public:
        explicit SyncHandle(uint32 file) : file_(file) {}

        int64 Tell() override
        {
            return offset_;
        }

        bool Seek(int64 value) override
        {
            if (value < 0 || value > file_size())
            {
                return false;
            }
            offset_ = value;
            return true;
        }

        bool SeekFromEnd(int64 value) override
        {
            return Seek(file_size() + value);
        }

        bool Read(uint8* dest, int64 count) override
        {
            if (count < 0 || count > file_size() - offset_)
            {
                return false;
            }
            while (count)
            {
                const auto chunk = static_cast<uint32>(std::min<int64>(count, 1024 * 1024));
                if (!kh02_remote_read(file_, offset_, chunk, dest))
                {
                    return false;
                }
                offset_ += chunk;
                dest += chunk;
                count -= chunk;
            }
            return true;
        }

        bool Write(const uint8*, int64) override
        {
            return false;
        }

        int64 Size() override
        {
            return file_size();
        }
    };

    class MemoryRequest final : public IAsyncReadRequest
    {
      public:
        MemoryRequest(uint32 file, FAsyncFileCallBack* cb) : IAsyncReadRequest(cb, true, nullptr)
        {
            Size = kh02_remote_file_size(file);
            kh02_async_event(0, file, Size);
            SetComplete();
        }

        MemoryRequest(uint32 file, int64 offset, int64 count, EAsyncIOPriority priority, FAsyncFileCallBack* cb,
                      uint8* supplied)
            : IAsyncReadRequest(cb, false, supplied)
        {
            kh02_async_event(1, offset, count);
            if (priority == AIOP_Precache)
            {
                Memory = nullptr;
                SetComplete();
                return;
            }
            const auto size = kh02_remote_file_size(file);
            if (offset < 0 || count < 0 || offset > size || count > size - offset)
            {
                Memory = nullptr;
                bCanceled = true;
                SetComplete();
                return;
            }
            if (!count)
            {
                SetComplete();
                return;
            }
            task_ = std::make_shared<kh02asset::ReadTask>([this, file, offset, count](kh02asset::ReadTask& task)
                                                          { read(task, file, offset, count); });
            kh3ap::kh02_async_reads().submit(task_);
        }

      private:
        std::shared_ptr<kh02asset::ReadTask> task_;

        void read(kh02asset::ReadTask& task, uint32 file, int64 offset, int64 count) noexcept
        {
            bool succeeded = false;
            try
            {
                if (!task.canceled())
                {
                    if (!Memory)
                    {
                        Memory = static_cast<uint8*>(FMemory::Malloc(count));
                    }
                    if (Memory)
                    {
                        succeeded = true;
                        // Cancellation cannot interrupt an authenticated pipe
                        // frame. Finish that frame, then stop before another.
                        while (count && !task.canceled())
                        {
                            const auto chunk = static_cast<uint32>(std::min<int64>(count, 1024 * 1024));
                            if (!kh02_remote_read(file, offset, chunk, Memory + bytes_read_))
                            {
                                succeeded = false;
                                break;
                            }
                            bytes_read_ += chunk;
                            offset += chunk;
                            count -= chunk;
                        }
                    }
                }
            }
            catch (...)
            {
                // A catalog/cache allocation failure is a failed read, never an
                // exception escaping the worker or a partially valid package.
                succeeded = false;
            }
            if (!succeeded || task.canceled())
            {
                bCanceled = true;
                task.cancel();
            }
            SetComplete();
        }

        int64 bytes_read_{};

      public:
        ~MemoryRequest() override
        {
            if (task_)
            {
                task_->wait();
            }
            if (!bSizeRequest && Memory)
            {
                if (!bUserSuppliedMemory)
                {
                    FMemory::Free(Memory);
                }
                Memory = nullptr;
            }
        }

        void WaitCompletionImpl(float seconds) override
        {
            if (task_)
            {
                task_->wait(seconds);
            }
        }

        void CancelImpl() override
        {
            if (task_)
            {
                task_->cancel();
            }
        }
    };

    class AsyncHandle final : public IAsyncReadFileHandle
    {
        uint32 file_;

      public:
        explicit AsyncHandle(uint32 file) : file_(file) {}

        IAsyncReadRequest* SizeRequest(FAsyncFileCallBack* cb) override
        {
            return new MemoryRequest(file_, cb);
        }

        IAsyncReadRequest* ReadRequest(int64 offset, int64 count, EAsyncIOPriority priority, FAsyncFileCallBack* cb,
                                       uint8* memory) override
        {
            return new MemoryRequest(file_, offset, count, priority, cb, memory);
        }
    };
} // namespace

extern "C" void* kh02_make_sync_handle(unsigned int file)
{
    return new SyncHandle(file);
}

extern "C" void* kh02_make_async_handle(unsigned int file)
{
    return new AsyncHandle(file);
}

extern "C" unsigned int kh02_async_request_size()
{
    return sizeof(IAsyncReadRequest);
}
