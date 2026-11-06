#pragma once
#include "kh02_asset_companion.h"
#include <memory>
#include <stop_token>
#include <vector>

namespace kh3ap
{
    // Sole owner of the verified child process, kill-on-close job and private desktop.
    class Kh02CompanionProcess
    {
      public:
        Kh02CompanionProcess();
        ~Kh02CompanionProcess();
        auto start(std::wstring_view executable, const std::vector<std::wstring>& store_arguments = {}) -> Kh02CompanionSession;
        auto session() -> Kh02CompanionSession;
        auto health() const -> Kh02CompanionSession;
        auto stop(const kh02control::Identity& expected) -> bool;
        auto stop() -> std::string;
        auto status() -> std::string;
        void set_ready(const kh02control::Identity& identity);
        void wait_assets_ready(std::stop_token stopping) const;
        auto submit(kh02control::Request request) -> std::shared_ptr<kh02control::Ticket>;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
