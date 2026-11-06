#include "kh3ap/features/grants/messages/ap_message_param_storage.h"

#include <utility>

namespace kh3ap
{
    auto RuntimeApMessageParamStorage::retain_bytes(std::vector<std::byte> params, size_t max_entries) -> void
    {
        if (params.empty())
        {
            return;
        }

        std::scoped_lock lock{byte_mutex_};
        byte_storage_.emplace_back(std::move(params));
        while (byte_storage_.size() > max_entries)
        {
            byte_storage_.erase(byte_storage_.begin());
        }
    }

    auto RuntimeApMessageParamStorage::retain_string_array(std::wstring_view value, size_t max_entries)
            -> RetainedFStringArray
    {
        std::scoped_lock lock{string_array_mutex_};
        auto& storage = string_array_storage_.emplace_back();
        storage.emplace_back(std::wstring{value}.c_str());
        while (string_array_storage_.size() > max_entries)
        {
            string_array_storage_.erase(string_array_storage_.begin());
        }

        return RetainedFStringArray{
                .data_ptr = reinterpret_cast<uint64_t>(storage.data()),
                .num = static_cast<int32_t>(storage.size()),
                .max = static_cast<int32_t>(storage.size())};
    }
}
