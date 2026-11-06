#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include <iterator>
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/transport/bounded_read.h"
#include "kh3ap/bridge/transport/pipe_security.h"
#include "kh3ap/bridge/transport/local_socket.h"
#include "kh3ap/bridge/serialization/request_parser.h"
#include "kh3ap/bridge/rewards/trap_queue.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/bridge/checks/lucky_marks.h"
#include "kh3ap/bridge/checks/level_checks.h"
#include "kh3ap/bridge/session/deathlink.h"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <iomanip>
#include <fstream>
#include <map>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace kh3ap::pipe
{
    namespace
    {
        constexpr auto wire_byte_bits = 8;
        constexpr auto wire_byte_mask = 255;
        constexpr auto pipe_io_wait_index = 2;
        constexpr auto pipe_name_hash_offset_basis = 14695981039346656037ULL;
        constexpr auto pipe_name_hash_prime = 1099511628211ULL;
        constexpr auto pipe_hash_hex_digits = 16;
        constexpr auto max_pipe_record_count = 4096;
        constexpr auto pipe_patch_payload_headroom = 4096;
        constexpr auto pipe_packet_header_bytes = 16;
        constexpr auto pipe_record_header_bytes = 15;
        constexpr auto pipe_magic_bytes = 4;
        constexpr auto pipe_io_timeout_ms = 5000;
        constexpr auto pipe_handshake_timeout_ms = 5000;
        constexpr auto pipe_buffer_bytes = 65536;
        constexpr auto pipe_listener_retry_ms = 500;
        constexpr auto pipe_set_operation = 1;
        constexpr auto pipe_delete_operation = 2;
        constexpr auto pipe_acknowledge_operation = 3;
        constexpr auto pipe_patch_operation = 4;

        constexpr size_t max_packet = 8 * 1024 * 1024;
        constexpr auto max_document_value_bytes = max_packet / 2;
        constexpr size_t max_events = 16384;
        using channels::checkpoint_channel;
        using channels::client_channel;
        using channels::event_channel;
        using channels::known;
        struct Record
        {
            uint8_t op{pipe_set_operation};
            std::string name;
            uint64_t seq{};
            std::string value;
        };
        std::mutex mutex;
        std::condition_variable_any incoming;
        bool transport_stopping{false};
        std::map<std::string, Record> values;
        std::map<std::string, std::shared_ptr<const RuntimeItemGrantBatchRequest>> prepared_grants;
        // Retain authority after deletion, including SET+DELETE between two
        // game-thread updates. A missing live value must not revive disk data.
        std::map<std::string, uint64_t> received_revisions;
        std::map<std::string, Record> published;
        std::map<std::string, std::deque<Record>> events;
        uint64_t sequence{};
        std::atomic<bool> updates{false};
        std::atomic<bool> grant_updates{false};
        std::atomic<uint64_t> input_generation_counter{};
        std::thread worker;
        HANDLE stopped{};
        HANDLE changed{};

        auto signal() -> void
        {
            if (changed)
            {
                SetEvent(changed);
            }
        }
        auto name_for(const std::filesystem::path& root) -> std::wstring
        {
            auto path = std::filesystem::absolute(root).lexically_normal().wstring();
            while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
            {
                path.pop_back();
            }
            uint64_t hash = pipe_name_hash_offset_basis;
            for (auto ch : path)
            {
                if (ch == L'/')
                {
                    ch = L'\\';
                }
                if (ch >= L'A' && ch <= L'Z')
                {
                    ch += L'a' - L'A';
                }
                hash = (hash ^ (ch & wire_byte_mask)) * pipe_name_hash_prime;
                hash = (hash ^ (ch >> wire_byte_bits)) * pipe_name_hash_prime;
            }
            std::wostringstream out;
            out << L"\\\\.\\pipe\\KH3AP-v3-" << std::hex << std::setw(pipe_hash_hex_digits)
                << std::setfill(L'0') << hash;
            return out.str();
        }
        auto put(std::string& out, uint64_t value, size_t bytes) -> void
        {
            for (size_t i = 0; i < bytes; ++i)
            {
                out.push_back(static_cast<char>(value >> (i * wire_byte_bits)));
            }
        }
        auto get(std::string_view in, size_t& pos, size_t bytes) -> uint64_t
        {
            if (pos + bytes > in.size())
            {
                throw std::runtime_error("truncated pipe frame");
            }
            uint64_t value{};
            for (size_t i = 0; i < bytes; ++i)
            {
                value |= uint64_t(static_cast<unsigned char>(in[pos++])) << (i * wire_byte_bits);
            }
            return value;
        }
        auto parse(std::string_view in, uint64_t& cursor) -> std::vector<Record>
        {
            if (in.size() < pipe_packet_header_bytes || in.substr(0, pipe_magic_bytes) != "K3P3")
            {
                throw std::runtime_error("pipe protocol version");
            }
            size_t pos = pipe_magic_bytes;
            cursor = get(in, pos, sizeof(uint64_t));
            const auto count = get(in, pos, sizeof(uint32_t));
            if (count > max_pipe_record_count)
            {
                throw std::runtime_error("pipe record limit");
            }
            std::vector<Record> result;
            for (uint64_t i = 0; i < count; ++i)
            {
                Record r;
                r.op = static_cast<uint8_t>(get(in, pos, sizeof(uint8_t)));
                const auto key_size = get(in, pos, sizeof(uint16_t));
                r.seq = get(in, pos, sizeof(uint64_t));
                const auto size = get(in, pos, sizeof(uint32_t));
                if (key_size + size > in.size() - pos)
                {
                    throw std::runtime_error("pipe payload limit");
                }
                r.name = in.substr(pos, key_size);
                pos += key_size;
                r.value = in.substr(pos, size);
                pos += size;
                if (!channels::allows_client_operation(r.name, r.op))
                {
                    throw std::runtime_error("pipe channel direction");
                }
                result.push_back(std::move(r));
            }
            if (pos != in.size())
            {
                throw std::runtime_error("pipe trailing bytes");
            }
            return result;
        }
        auto append(std::string& out, const Record& r) -> void
        {
            if (out.size() + pipe_record_header_bytes + r.name.size() + r.value.size() > max_packet)
            {
                throw std::runtime_error("pipe snapshot too large");
            }
            put(out, r.op, sizeof(uint8_t));
            put(out, r.name.size(), sizeof(uint16_t));
            put(out, r.seq, sizeof(uint64_t));
            put(out, r.value.size(), sizeof(uint32_t));
            out += r.name;
            out += r.value;
        }
        auto accept(const std::vector<Record>& records, std::map<std::string, Record>& bases)
            -> void
        {
            // Keep parsed grants tied to their exact record. A later record
            // with the same channel may be malformed or never applied.
            std::vector<std::shared_ptr<const RuntimeItemGrantBatchRequest>> prepared(records.size());
            for (size_t index = 0; index < records.size(); ++index)
            {
                const auto& r = records[index];
                if (r.op == pipe_set_operation &&
                    (r.name == channels::runtime_item_grant ||
                     r.name == channels::runtime_item_grant_priority ||
                     r.name == channels::runtime_trap_queue_request))
                {
                    if (auto batch = RuntimeRequestParser::parse_item_grant_batch(r.value))
                    {
                        prepared[index] =
                            std::make_shared<const RuntimeItemGrantBatchRequest>(std::move(*batch));
                    }
                }
            }
            for (const auto& r : records)
            {
                if (r.op == pipe_set_operation && r.name == channels::runtime_receipt_catalog)
                {
                    reconciliation::observe(r.name, r.value);
                }
            }
            for (const auto& r : records)
            {
                if (r.op == pipe_set_operation && r.name == channels::runtime_deathlink_control)
                {
                    deathlink::observe(r.name, r.value);
                }
            }
            // Parsing must finish before taking this mutex: a game-thread
            // snapshot read must never wait behind a large JSON parse.
            std::lock_guard lock{mutex};
            for (size_t index = 0; index < records.size(); ++index)
            {
                auto r = records[index];
                if (r.op == pipe_patch_operation)
                {
                    size_t pos{};
                    const auto revision = get(r.value, pos, sizeof(uint64_t));
                    const auto prefix = get(r.value, pos, sizeof(uint32_t));
                    const auto suffix = get(r.value, pos, sizeof(uint32_t));
                    const auto base = bases.find(r.name);
                    if (base == bases.end() || base->second.seq != revision ||
                        prefix + suffix > base->second.value.size() ||
                        prefix + suffix + r.value.size() - pos >
                            max_packet - pipe_patch_payload_headroom)
                    {
                        throw std::runtime_error("pipe patch base or bounds mismatch");
                    }
                    const auto& previous = base->second.value;
                    r.value = previous.substr(0, prefix) + r.value.substr(pos) +
                              previous.substr(previous.size() - suffix);
                    r.op = pipe_set_operation;
                }
                if (r.op == pipe_set_operation && checkpoint_channel(r.name))
                {
                    bases[r.name] = r;
                }
                if (r.op == pipe_delete_operation)
                {
                    bases.erase(r.name);
                }
                if (r.op == pipe_acknowledge_operation)
                {
                    auto& queue = events[r.name];
                    if (!queue.empty() && queue.front().seq == r.seq)
                    {
                        queue.pop_front();
                    }
                }
                else if (r.op == pipe_delete_operation)
                {
                    // Conditional removal cannot delete a newer command.
                    auto it = values.find(r.name);
                    if ((it != values.end() && (r.value.empty() || it->second.value == r.value)) ||
                        (it == values.end() && r.value.empty() &&
                         !received_revisions.contains(r.name)))
                    {
                        if (it != values.end())
                        {
                            values.erase(it);
                        }
                        prepared_grants.erase(r.name);
                        received_revisions[r.name] = ++sequence;
                        updates.store(true, std::memory_order_release);
                        if (r.name == channels::runtime_item_grant ||
                            r.name == channels::runtime_item_grant_priority ||
                            r.name == channels::runtime_trap_queue_request)
                        {
                            grant_updates.store(true, std::memory_order_release);
                        }
                        input_generation_counter.fetch_add(1, std::memory_order_release);
                    }
                }
                else
                {
                    auto it = values.find(r.name);
                    if (it == values.end() || it->second.value != r.value)
                    {
                        // Checkpoint patches have been reconstructed above. Pass
                        // the complete external result snapshot to the worker.
                        if (r.name == channels::runtime_mom_clear_state)
                        {
                            reconciliation::observe(r.name, r.value);
                        }
                        auto copy = r;
                        copy.seq = ++sequence;
                        received_revisions[r.name] = copy.seq;
                        values[r.name] = std::move(copy);
                        if (r.name == channels::runtime_item_grant ||
                            r.name == channels::runtime_item_grant_priority ||
                            r.name == channels::runtime_trap_queue_request)
                        {
                            prepared_grants[r.name] = prepared[index];
                        }
                        updates.store(true, std::memory_order_release);
                        if (r.name == channels::runtime_item_grant ||
                            r.name == channels::runtime_item_grant_priority ||
                            r.name == channels::runtime_trap_queue_request)
                        {
                            grant_updates.store(true, std::memory_order_release);
                        }
                        input_generation_counter.fetch_add(1, std::memory_order_release);
                    }
                }
            }
            // An ACK may expose the next queued event. Wake the independent
            // sender, never wait for a game-thread publication or a timer.
            signal();
            incoming.notify_all();
        }
        auto outgoing(uint64_t cursor, std::map<std::string, uint64_t>& sent_events) -> std::string
        {
            std::lock_guard lock{mutex};
            std::vector<Record> snapshots;
            for (const auto& [name, r] : published)
            {
                if (r.seq > cursor)
                {
                    snapshots.push_back(r);
                }
            }
            std::sort(snapshots.begin(), snapshots.end(),
                      [](const auto& a, const auto& b)
                      {
                          return a.seq < b.seq;
                      });
            std::string body;
            uint32_t count{};
            uint64_t next_cursor = sequence;
            for (const auto& r : snapshots)
            {
                if (pipe_packet_header_bytes + body.size() + pipe_record_header_bytes +
                        r.name.size() + r.value.size() >
                    max_packet)
                {
                    next_cursor = cursor;
                    break;
                }
                append(body, r);
                ++count;
                cursor = r.seq;
            }
            for (const auto& [name, queue] : events)
            {
                if (!queue.empty() && sent_events[name] != queue.front().seq &&
                    pipe_packet_header_bytes + body.size() + pipe_record_header_bytes +
                            name.size() + queue.front().value.size() <=
                        max_packet)
                {
                    append(body, queue.front());
                    ++count;
                    sent_events[name] = queue.front().seq;
                }
            }
            std::string out = "K3P3";
            put(out, next_cursor, sizeof(uint64_t));
            put(out, count, sizeof(uint32_t));
            out += body;
            return out;
        }
        // Cancellation is drained before OVERLAPPED/buffer storage is released.
        auto finish(HANDLE pipe, OVERLAPPED& ov, BOOL immediate, DWORD& bytes, HANDLE disconnected,
                    DWORD timeout = pipe_io_timeout_ms) -> bool
        {
            bool ok = immediate != FALSE;
            if (!ok && GetLastError() == ERROR_IO_PENDING)
            {
                HANDLE waits[] = {stopped, disconnected, ov.hEvent};
                const auto result = WaitForMultipleObjects(static_cast<DWORD>(std::size(waits)),
                                                           waits, FALSE, timeout);
                ok = result == WAIT_OBJECT_0 + pipe_io_wait_index;
                if (!ok)
                {
                    CancelIoEx(pipe, &ov);
                }
                ok = GetOverlappedResult(pipe, &ov, &bytes, TRUE) && ok;
            }
            return ok;
        }
        auto send_updates(HANDLE pipe, HANDLE disconnected) -> void
        {
            const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!event)
            {
                SetEvent(disconnected);
                return;
            }
            try
            {
                uint64_t cursor{};
                std::map<std::string, uint64_t> sent_events;
                while (WaitForSingleObject(stopped, 0) != WAIT_OBJECT_0 &&
                       WaitForSingleObject(disconnected, 0) != WAIT_OBJECT_0)
                {
                    auto packet = outgoing(cursor, sent_events);
                    if (packet.size() == pipe_packet_header_bytes)
                    {
                        HANDLE waits[] = {stopped, disconnected, changed};
                        if (WaitForMultipleObjects(static_cast<DWORD>(std::size(waits)), waits,
                                                   FALSE,
                                                   INFINITE) != WAIT_OBJECT_0 + pipe_io_wait_index)
                        {
                            break;
                        }
                        continue;
                    }
                    OVERLAPPED ov{};
                    ov.hEvent = event;
                    ResetEvent(event);
                    DWORD bytes{};
                    if (!finish(pipe, ov,
                                WriteFile(pipe, packet.data(), static_cast<DWORD>(packet.size()),
                                          &bytes, &ov),
                                bytes, disconnected) ||
                        bytes != packet.size())
                    {
                        break;
                    }
                    size_t pos = pipe_magic_bytes;
                    cursor = get(packet, pos, sizeof(uint64_t));
                }
            }
            catch (const std::exception&)
            { /* disconnect and replay retained state on reconnect */
            }
            SetEvent(disconnected);
            CloseHandle(event);
        }
        auto run(std::wstring name, std::shared_ptr<UserPipeSecurity> security) -> void
        {
            FILETIME created{};
            GetSystemTimeAsFileTime(&created);
            const uint64_t epoch = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            std::vector<char> buffer(max_packet);
            while (WaitForSingleObject(stopped, 0) != WAIT_OBJECT_0)
            {
                const HANDLE pipe = CreateNamedPipeW(
                    name.c_str(),
                    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
                        PIPE_REJECT_REMOTE_CLIENTS,
                    1, pipe_buffer_bytes, pipe_buffer_bytes, 0, security->attributes());
                if (pipe == INVALID_HANDLE_VALUE)
                {
                    if (WaitForSingleObject(stopped, pipe_listener_retry_ms) == WAIT_OBJECT_0)
                    {
                        break;
                    }
                    continue;
                }
                const HANDLE disconnected = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!disconnected)
                {
                    CloseHandle(pipe);
                    break;
                }
                std::thread sender;
                std::map<std::string, Record> bases;
                OVERLAPPED ov{};
                ov.hEvent = event;
                ResetEvent(event);
                DWORD bytes{};
                const BOOL connected = ConnectNamedPipe(pipe, &ov);
                const DWORD error = connected ? ERROR_SUCCESS : GetLastError();
                bool ready = connected || error == ERROR_PIPE_CONNECTED;
                if (!ready && error == ERROR_IO_PENDING)
                {
                    HANDLE waits[] = {stopped, event};
                    ready = WaitForMultipleObjects(static_cast<DWORD>(std::size(waits)), waits,
                                                   FALSE, INFINITE) == WAIT_OBJECT_0 + 1;
                    if (!ready)
                    {
                        CancelIoEx(pipe, &ov);
                    }
                    ready = GetOverlappedResult(pipe, &ov, &bytes, TRUE) && ready;
                }
                try
                {
                    // Explicit version/instance handshake before any commands.
                    if (ready)
                    {
                        ov = {};
                        ov.hEvent = event;
                        ResetEvent(event);
                        ready = finish(pipe, ov,
                                       ReadFile(pipe, buffer.data(),
                                                static_cast<DWORD>(buffer.size()), &bytes, &ov),
                                       bytes, disconnected) &&
                                bytes == pipe_magic_bytes &&
                                std::string_view(buffer.data(), bytes) == "K3P3";
                        if (ready)
                        {
                            std::string hello = "K3P3";
                            put(hello, epoch, sizeof(uint64_t));
                            ov = {};
                            ov.hEvent = event;
                            ResetEvent(event);
                            ready = finish(pipe, ov,
                                           WriteFile(pipe, hello.data(),
                                                     static_cast<DWORD>(hello.size()), &bytes, &ov),
                                           bytes, disconnected);
                        }
                    }
                    if (ready)
                    {
                        sender = std::thread(send_updates, pipe, disconnected);
                    }
                    while (ready && WaitForSingleObject(stopped, 0) != WAIT_OBJECT_0)
                    {
                        ov = {};
                        ov.hEvent = event;
                        ResetEvent(event);
                        // No idle timeout: peer input, disconnect or shutdown
                        // completes/cancels this read. Sending proceeds independently.
                        if (!finish(pipe, ov,
                                    ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                                             &bytes, &ov),
                                    bytes, disconnected, INFINITE))
                        {
                            break;
                        }
                        uint64_t cursor{};
                        const auto records = parse(std::string_view(buffer.data(), bytes), cursor);
                        accept(records, bases);
                    }
                }
                catch (const std::exception&)
                { /* malformed peer: disconnect, leave game state intact */
                }
                SetEvent(disconnected);
                if (sender.joinable())
                {
                    sender.join();
                }
                deathlink::disconnected();
                erase(channels::runtime_gameflow_command);
                erase(channels::runtime_warp_command);
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
                CloseHandle(disconnected);
            }
            CloseHandle(event);
        }

        auto send_socket_updates(local_ipc::Stream& stream, HANDLE disconnected) -> void
        {
            try
            {
                uint64_t cursor{};
                std::map<std::string, uint64_t> sent_events;
                while (WaitForSingleObject(stopped, 0) != WAIT_OBJECT_0 &&
                       WaitForSingleObject(disconnected, 0) != WAIT_OBJECT_0)
                {
                    auto packet = outgoing(cursor, sent_events);
                    if (packet.size() == pipe_packet_header_bytes)
                    {
                        HANDLE waits[]{stopped, disconnected, changed};
                        if (WaitForMultipleObjects(static_cast<DWORD>(std::size(waits)), waits,
                                                   FALSE,
                                                   INFINITE) != WAIT_OBJECT_0 + pipe_io_wait_index)
                        {
                            break;
                        }
                        continue;
                    }
                    stream.send(packet, stopped, disconnected);
                    size_t pos = pipe_magic_bytes;
                    cursor = get(packet, pos, sizeof(uint64_t));
                }
            }
            catch (const std::exception&)
            { /* retain unacknowledged events for reconnect */
            }
            SetEvent(disconnected);
        }
        auto run_socket(std::filesystem::path root) -> void
        {
            FILETIME created{};
            GetSystemTimeAsFileTime(&created);
            const uint64_t epoch = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            while (WaitForSingleObject(stopped, 0) != WAIT_OBJECT_0)
            {
                try
                {
                    local_ipc::Server server(root);
                    while (auto stream = server.accept(stopped))
                    {
                        const auto disconnected = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                        if (!disconnected)
                        {
                            throw std::runtime_error(
                                "local IPC disconnect event allocation failed");
                        }
                        std::thread sender;
                        try
                        {
                            if (stream->receive(stopped, disconnected, pipe_handshake_timeout_ms) !=
                                "K3P3" + server.token())
                            {
                                throw std::runtime_error("local IPC authentication failed");
                            }
                            std::string hello = "K3P3";
                            put(hello, epoch, sizeof(uint64_t));
                            stream->send(hello, stopped, disconnected);
                            sender =
                                std::thread(send_socket_updates, std::ref(*stream), disconnected);
                            std::map<std::string, Record> bases;
                            while (WaitForSingleObject(stopped, 0) != WAIT_OBJECT_0)
                            {
                                const auto packet = stream->receive(stopped, disconnected);
                                uint64_t cursor{};
                                accept(parse(packet, cursor), bases);
                            }
                        }
                        catch (const std::exception&)
                        { /* close peer, preserve gameplay state */
                        }
                        SetEvent(disconnected);
                        if (sender.joinable())
                        {
                            sender.join();
                        }
                        deathlink::disconnected();
                        erase(channels::runtime_gameflow_command);
                        erase(channels::runtime_warp_command);
                        CloseHandle(disconnected);
                    }
                }
                catch (const std::exception&)
                { /* retry listener setup without touching gameplay state */
                }
                if (WaitForSingleObject(stopped, pipe_listener_retry_ms) == WAIT_OBJECT_0)
                {
                    break;
                }
            }
        }
    } // namespace
    auto manages(const std::filesystem::path& path) -> bool
    {
        return known(path.filename().string());
    }
    auto durable(const std::filesystem::path& path) -> bool
    {
        return channels::durable_native_channel(path.filename().string());
    }
    auto take_updates() -> bool
    {
        return updates.exchange(false, std::memory_order_acq_rel);
    }
    auto take_grant_updates() -> bool
    {
        return grant_updates.exchange(false, std::memory_order_acq_rel);
    }
    auto input_generation() -> uint64_t
    {
        return input_generation_counter.load(std::memory_order_acquire);
    }
    auto read(const std::filesystem::path& path) -> std::optional<std::string>
    {
        std::lock_guard lock{mutex};
        const auto it = values.find(path.filename().string());
        return it == values.end() ? std::nullopt : std::optional{it->second.value};
    }
    auto grant_batch(const std::filesystem::path& path)
        -> std::pair<uint64_t, std::shared_ptr<const RuntimeItemGrantBatchRequest>>
    {
        std::lock_guard lock{mutex};
        const auto name = path.filename().string();
        const auto revision = received_revisions.find(name);
        const auto batch = prepared_grants.find(name);
        return {revision == received_revisions.end() ? 0 : revision->second,
                batch == prepared_grants.end() ? nullptr : batch->second};
    }
    auto wait_and_take(const std::filesystem::path& path, std::stop_token cancellation,
                       std::chrono::milliseconds timeout) -> std::optional<std::string>
    {
        const auto name = path.filename().string();
        std::unique_lock lock{mutex};
        const auto ready = incoming.wait_for(lock, cancellation, timeout,
                                             [&]
                                             {
                                                 return transport_stopping || values.contains(name);
                                             });
        if (!ready || transport_stopping || cancellation.stop_requested())
        {
            return std::nullopt;
        }
        auto value = std::move(values.at(name).value);
        values.erase(name);
        published[name] = Record{pipe_delete_operation, name, ++sequence, {}};
        signal();
        return value;
    }
    auto revision(const std::filesystem::path& path) -> uint64_t
    {
        std::lock_guard lock{mutex};
        const auto it = values.find(path.filename().string());
        return it == values.end() ? 0 : it->second.seq;
    }
    auto input_revision(const std::filesystem::path& path) -> uint64_t
    {
        std::lock_guard lock{mutex};
        const auto received = received_revisions.find(path.filename().string());
        return received == received_revisions.end() ? 0 : received->second;
    }
    auto write(const std::filesystem::path& path, std::string_view value) -> bool
    {
        if (value.size() > max_document_value_bytes)
        {
            return false;
        }
        const auto name = path.filename().string();
        std::lock_guard lock{mutex};
        const auto it = values.find(name);
        if (it != values.end() && it->second.value == value)
        {
            return true;
        }
        if (event_channel(name) && events[name].size() >= max_events)
        {
            return false;
        }
        // Only changed native events enter the reconciliation worker. These
        // small records preserve producer ordering; JSON/file work stays there.
        reconciliation::observe(name, value);
        deathlink::observe(name, value);
        Record r{pipe_set_operation, name, ++sequence, std::string(value)};
        values[name] = r;
        if (event_channel(name))
        {
            events[name].push_back(r);
        }
        else
        {
            published[name] = r;
        }
        signal();
        return true;
    }
    auto submit_native_grant(const std::filesystem::path& path, std::string_view value) -> bool
    {
        const auto name = path.filename().string();
        if (name != channels::runtime_item_grant_priority)
        {
            return false;
        }
        auto batch = RuntimeRequestParser::parse_item_grant_batch(std::string(value));
        if (!batch)
        {
            return false;
        }
        auto prepared = std::make_shared<const RuntimeItemGrantBatchRequest>(std::move(*batch));
        std::lock_guard lock{mutex};
        Record r{pipe_set_operation, name, ++sequence, std::string(value)};
        received_revisions[name] = r.seq;
        values[name] = std::move(r);
        prepared_grants[name] = std::move(prepared);
        updates.store(true, std::memory_order_release);
        grant_updates.store(true, std::memory_order_release);
        signal();
        return true;
    }
    auto erase(const std::filesystem::path& path) -> void
    {
        const auto name = path.filename().string();
        std::lock_guard lock{mutex};
        const auto it = values.find(name);
        if (it == values.end())
        {
            return;
        }
        published[name] = Record{pipe_delete_operation, name, ++sequence,
                                 name == channels::runtime_kh02_launch_response ? std::string{}
                                                                                : it->second.value};
        values.erase(it);
        prepared_grants.erase(name);
        signal();
    }
    auto start(const std::filesystem::path& root, Transport transport) -> void
    {
        if (worker.joinable())
        {
            return;
        }
        const bool wine =
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
        const bool use_socket =
            transport == Transport::LocalTcp || (transport == Transport::Auto && wine);
        // Resolve the security descriptor on the caller before starting any
        // services. A failed identity lookup must not escape a worker thread.
        auto security =
            use_socket ? std::shared_ptr<UserPipeSecurity>{} : std::make_shared<UserPipeSecurity>();
        native_traps::start(root);
        reconciliation::start(root);
        lucky_marks::start(root);
        level_checks::start(root);
        deathlink::start(root);
        // One-time recovery of native-owned progression checkpoints, before
        // their services initialize. This is persistence, never live IPC.
        for (const auto& channel : channels::registry)
        {
            const auto path = root / channel.name;
            if (!durable(path))
            {
                continue;
            }
            std::ifstream input(path, std::ios::binary);
            if (input)
            {
                if (const auto content = read_stream_bounded(input, max_document_value_bytes))
                {
                    write(path, *content);
                }
            }
        }
        {
            std::lock_guard lock{mutex};
            transport_stopping = false;
            stopped = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        }
        if (use_socket)
        {
            worker = std::thread(run_socket, root);
        }
        else
        {
            worker = std::thread(run, name_for(root), std::move(security));
        }
    }
    auto stop() -> void
    {
        if (!worker.joinable())
        {
            return;
        }
        {
            std::lock_guard lock{mutex};
            transport_stopping = true;
        }
        incoming.notify_all();
        SetEvent(stopped);
        worker.join();
        native_traps::stop();
        reconciliation::stop();
        lucky_marks::stop();
        level_checks::stop();
        deathlink::stop();
        std::lock_guard lock{mutex};
        CloseHandle(changed);
        changed = nullptr;
        CloseHandle(stopped);
        stopped = nullptr;
    }
    auto notify_grants() -> void
    {
        grant_updates.store(true, std::memory_order_release);
    }
} // namespace kh3ap::pipe
