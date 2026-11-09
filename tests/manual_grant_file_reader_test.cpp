#include "kh3ap/diagnostics/grants/manual_grant_file_reader.h"
#include <atomic>
#include <cassert>
#include <fstream>
#include <thread>

int main()
{
    using namespace std::chrono_literals;
    const auto root = std::filesystem::temp_directory_path() /
        ("kh3-manual-read-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    const auto path = root / "request.json";
    const auto other = root / "other.json";
    auto write = [](const auto& target, const char* value) { std::ofstream(target) << value; };
    auto read = [](const auto& target) -> std::optional<std::string> {
        std::ifstream input(target);
        std::string text;
        if (!std::getline(input, text)) return std::nullopt;
        return text;
    };
    auto await_text = [&](auto& reader, const auto& target, auto load) -> std::string {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (const auto* text = reader.poll(target, load)) return *text;
            std::this_thread::sleep_for(1ms);
        }
        assert(false && "Read failed to complete");
        return {};
    };
    {
        kh3ap::ManualGrantFileReader reader;
        std::atomic<int> reads{};
        auto counted = [&](const auto& target) { ++reads; return read(target); };
        assert(!reader.poll(path, counted));
        write(path, "first");
        assert(await_text(reader, path, counted) == "first");
        assert(!reader.ready()); // Completion wake retires when consumed.
        for (int i = 0; i < 20; ++i) assert(*reader.poll(path, counted) == "first");
        assert(reads == 1); // Blocked gameplay retries reuse the completed read.
        write(path, "replacement-longer");
        assert(await_text(reader, path, counted) == "replacement-longer");
        assert(reads == 2);
        std::filesystem::remove(path);
        assert(!reader.poll(path, counted)); // Removal invalidates cached grants.
    }
    {
        kh3ap::ManualGrantFileReader reader;
        write(path, "old");
        std::promise<void> entered, release;
        auto released = release.get_future().share();
        assert(!reader.poll(path, [&](const auto&) -> std::optional<std::string> {
            entered.set_value();
            released.wait();
            return "old";
        }));
        entered.get_future().wait();
        assert(!reader.ready());
        assert(!reader.poll(path, read)); // Must return while the worker is blocked.
        std::filesystem::remove(path);
        assert(!reader.poll(path, read));
        write(other, "new bridge request");
        release.set_value();
        assert(await_text(reader, other, read) == "new bridge request");
        // The late completion from the deleted request was never returned.
    }
    {
        kh3ap::ManualGrantFileReader reader;
        write(path, "old request");
        std::promise<void> entered, release;
        auto released = release.get_future().share();
        assert(!reader.poll(path, [&](const auto&) -> std::optional<std::string> {
            entered.set_value();
            released.wait();
            return "old request";
        }));
        entered.get_future().wait();
        write(path, "new request replacing an in-flight read");
        release.set_value();
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (!reader.ready() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
        assert(reader.ready());
        assert(await_text(reader, path, read) == "new request replacing an in-flight read");
    }
    {
        kh3ap::ManualGrantFileReader reader;
        write(path, "failed");
        std::atomic<int> attempts{};
        auto failed = [&](const auto&) -> std::optional<std::string> { ++attempts; throw 1; };
        for (int i = 0; i < 30; ++i)
        {
            assert(!reader.poll(path, failed));
            std::this_thread::sleep_for(1ms);
        }
        assert(attempts == 1); // Failed I/O is bounded, not polled indefinitely.
        write(path, "changed request");
        assert(await_text(reader, path, read) == "changed request");
    }
    std::filesystem::remove(path);
    std::filesystem::remove(other);
    std::filesystem::remove(root);
}
