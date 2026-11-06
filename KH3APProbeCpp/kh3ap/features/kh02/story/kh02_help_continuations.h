#pragma once
#include "kh02_map_session.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/NameTypes.hpp>
#include <cstdint>
#include <vector>

namespace kh3ap
{
    // Stores no VM frame/property pointers. Resume only after the originating
    // call has returned, while its weak world and map generation still match.
    class Kh02HelpContinuations
    {
      public:
        void enqueue(RC::Unreal::UObject* callback, RC::Unreal::UObject* world, RC::Unreal::FName execution,
                     int linkage, Kh02MapSession::Generation generation);
        void tick(RC::Unreal::UObject* world, const Kh02MapSession& session);
        void clear();
        auto completed() const -> uint64_t;

      private:
        struct Continuation
        {
            RC::Unreal::FWeakObjectPtr callback, world;
            RC::Unreal::FName execution;
            int linkage;
            Kh02MapSession::Generation generation;
        };

        std::vector<Continuation> pending;
        uint64_t continuation_count{};
    };
} // namespace kh3ap
