#pragma once
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <array>
#include <chrono>

namespace kh3ap
{
    // Bindings only: callers still read every ancestor's display/visibility live.
    // Owner changes and collected weak nodes rebuild immediately. A short topology
    // validation deadline also detects AS reparenting inside an unchanged movie.
    class MinimapAncestryCache
    {
    public:
        using Clock = std::chrono::steady_clock;
        using Object = RC::Unreal::UObject;
        static constexpr size_t maximum_ancestors = 16;
        static constexpr auto topology_validation_interval = std::chrono::milliseconds{100};
        void clear()
        {
            *this = {};
        }
        template <class Parent>
        auto sync(Object* head, Object* root, Clock::time_point now, Parent&& parent) -> bool
        {
            bool valid = bound_ && head_.Get() == head && root_.Get() == root;
            for (size_t i = 0; valid && i < count_; ++i)
            {
                valid = is_uobject_valid_fast(nodes_[i].Get());
            }
            if (valid && now < validate_)
            {
                return false;
            }
            std::array<RC::Unreal::FWeakObjectPtr, maximum_ancestors> next{};
            size_t count = 0;
            auto* current = head;
            while (current && count < next.size())
            {
                if (!is_uobject_valid_fast(current))
                {
                    clear();
                    return true;
                }
                next[count] = make_uobject_weak(current);
                if (next[count].Get() != current)
                {
                    clear();
                    return true;
                }
                ++count;
                current = parent(current);
            }
            if (current || !head || !is_uobject_valid_fast(root))
            {
                clear();
                return true;
            }
            bool changed = !valid || count != count_;
            for (size_t i = 0; !changed && i < count; ++i)
            {
                changed = nodes_[i].Get() != next[i].Get();
            }
            nodes_ = next;
            count_ = count;
            head_ = make_uobject_weak(head);
            root_ = make_uobject_weak(root);
            bound_ = head_.Get() == head && root_.Get() == root;
            validate_ = now + topology_validation_interval;
            return changed;
        }
        template <class Visit> auto visit(Visit&& visitor) const -> bool
        {
            if (!bound_)
            {
                return false;
            }
            for (size_t i = 0; i < count_; ++i)
            {
                auto* node = nodes_[i].Get();
                if (!is_uobject_valid_fast(node) || !visitor(node))
                {
                    return false;
                }
            }
            return true;
        }

    private:
        RC::Unreal::FWeakObjectPtr head_, root_;
        std::array<RC::Unreal::FWeakObjectPtr, maximum_ancestors> nodes_{};
        size_t count_{};
        bool bound_{};
        Clock::time_point validate_{};
    };
} // namespace kh3ap
