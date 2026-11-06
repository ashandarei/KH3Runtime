#pragma once
#include "kh3ap/bridge/rewards/reconciliation_protocol.h"
#include "kh3ap/bridge/rewards/receipt_accounting.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include <optional>
#include <tuple>

namespace kh3ap::reconciliation
{
    class ReconciliationProjections
    {
        using Json = json::Value;
        using ReceiptKey = std::tuple<uint64_t, uint64_t, std::map<int64_t, std::string>,
                                      std::set<int64_t>, std::vector<int64_t>, Json, Json,
                                      std::set<int64_t>, bool, std::string, int64_t>;
        using SaveKey =
            std::tuple<std::map<int64_t, Receipts>, std::set<int64_t>, uint64_t, bool, std::string>;

    public:
        struct ReceiptInputs
        {
            uint64_t accounting_revision;
            uint64_t catalog_revision;
            const std::map<int64_t, std::string>& policies;
            const std::set<int64_t>& active;
            const std::vector<int64_t>& pending;
            const Json& summary;
            const Json& repairs;
            const std::set<int64_t>& forced;
            bool ready;
            const std::string& reason;
            int64_t epoch;

            auto key() const
            {
                return std::tie(accounting_revision, catalog_revision, policies, active, pending,
                                summary, repairs, forced, ready, reason, epoch);
            }
        };

        struct SaveInputs
        {
            const std::map<int64_t, Receipts>& slots;
            const std::set<int64_t>& replay_eligible;
            uint64_t description_revision;
            bool catalog_loaded;
            const std::string& error;

            auto key() const
            {
                return std::tie(slots, replay_eligible, description_revision, catalog_loaded,
                                error);
            }
        };

        auto publish_receipts(const std::filesystem::path& root,
                              const receipt_accounting::Accounting& accounting,
                              const Json& identity, const Json::object_t& descriptions,
                              const std::map<int64_t, Json>& facts, ReceiptInputs inputs) -> void
        {
            const auto key = inputs.key();
            if (receipt_key && *receipt_key == key)
            {
                return;
            }
            const auto projected =
                accounting.project(identity, descriptions, facts, inputs.policies, inputs.active,
                                   inputs.pending, inputs.summary, inputs.repairs,
                                   Json::array_t(inputs.forced.begin(), inputs.forced.end()),
                                   inputs.ready, inputs.reason);
            for (const auto& [name, value] : projected)
            {
                const auto data = protocol::encode(value);
                if (bytes.contains(name) && bytes.at(name) == data)
                {
                    continue;
                }
                // The journal is already durable; these are reconstructed IPC views.
                bytes[name] = data;
                pipe::write(root / name, data);
            }
            receipt_key.emplace(key);
        }

        template <class BuildLedger>
        auto publish_saves(const std::filesystem::path& root, SaveInputs inputs,
                           BuildLedger&& build_ledger) -> void
        {
            const auto key = inputs.key();
            if (save_key && *save_key == key)
            {
                return;
            }
            if (pipe::write(root / channels::runtime_save_receipts,
                            protocol::encode(build_ledger())))
            {
                save_key.emplace(key);
            }
        }

    private:
        // Reference tuples above compare typed inputs before allocating/encoding
        // large views. Owned keys are updated only at the original publication boundary.
        std::optional<ReceiptKey> receipt_key;
        std::optional<SaveKey> save_key;
        std::map<std::string, std::string> bytes;
    };
} // namespace kh3ap::reconciliation
