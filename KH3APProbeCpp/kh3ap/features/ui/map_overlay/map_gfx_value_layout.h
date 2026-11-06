#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace kh3ap {
// Scalar reflection metadata only. Values are always read from the current
// native call's buffer, so neither UI state nor completion state is cached.
struct MapGfxValueLayout {
    int size{}, type{-1}, integer{-1}, number{-1}, number_size{}, object{-1};
    auto contains(int offset, size_t bytes) const -> bool {
        return size>0 && offset>=0 && bytes<=static_cast<size_t>(size) &&
            static_cast<size_t>(offset)<=static_cast<size_t>(size)-bytes;
    }
    auto read_type(const void* value) const -> std::optional<uint8_t> {
        if (!value || !contains(type,1)) return {};
        uint8_t result{};
        std::memcpy(&result,static_cast<const std::byte*>(value)+type,1);
        return result;
    }
    auto read_number(const void* value) const -> std::optional<double> {
        const auto kind=read_type(value);
        if (kind==3 && contains(integer,sizeof(int32_t))) {
            int32_t result{};
            std::memcpy(&result,static_cast<const std::byte*>(value)+integer,sizeof(result));
            return static_cast<double>(result);
        }
        if (kind!=4 || !contains(number,number_size)) return {};
        if (number_size==sizeof(float)) {
            float result{};
            std::memcpy(&result,static_cast<const std::byte*>(value)+number,sizeof(result));
            return static_cast<double>(result);
        }
        if (number_size==sizeof(double)) {
            double result{};
            std::memcpy(&result,static_cast<const std::byte*>(value)+number,sizeof(result));
            return result;
        }
        return {};
    }
    auto read_object(const void* value) const -> void* {
        const auto kind=read_type(value);
        if ((kind!=7 && kind!=8) || !contains(object,sizeof(void*))) return nullptr;
        void* result{};
        std::memcpy(&result,static_cast<const std::byte*>(value)+object,sizeof(result));
        return result;
    }
    auto write_type(void* value,uint8_t kind) const -> bool {
        if (!value || !contains(type,1)) return false;
        std::memcpy(static_cast<std::byte*>(value)+type,&kind,1);
        return true;
    }
    auto write_number(void* value,double input) const -> bool {
        if (!value || !contains(number,number_size) ||
            (number_size!=sizeof(float) && number_size!=sizeof(double)) || !contains(type,1)) return false;
        std::memset(value,0,size);
        write_type(value,4);
        if (number_size==sizeof(float)) {
            const float converted=static_cast<float>(input);
            std::memcpy(static_cast<std::byte*>(value)+number,&converted,sizeof(converted));
        } else std::memcpy(static_cast<std::byte*>(value)+number,&input,sizeof(input));
        return true;
    }
    auto write_object(void* value,void* input) const -> bool {
        if (!value || !input || !contains(object,sizeof(void*)) || !contains(type,1)) return false;
        std::memset(value,0,size);
        write_type(value,7);
        std::memcpy(static_cast<std::byte*>(value)+object,&input,sizeof(input));
        return true;
    }
};
}
