#include "kh3ap/bridge/transport/request_file_watcher.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <thread>
#include <type_traits>

namespace
{
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    void write_file(const std::filesystem::path& path, const char* content)
    {
        std::ofstream stream(path, std::ios::trunc);
        stream << content;
        stream.close();
        assert(stream.good());
    }

    bool await_change(kh3ap::RequestFileWatcher& watcher, const std::filesystem::path& root)
    {
        const auto deadline = Clock::now() + 1s;
        do
        {
            if (watcher.update(root))
            {
                return true;
            }
            std::this_thread::sleep_for(1ms);
        } while (Clock::now() < deadline);
        return false;
    }

    void assert_idle(kh3ap::RequestFileWatcher& watcher, const std::filesystem::path& root)
    {
        const auto deadline = Clock::now() + 30ms;
        do
        {
            assert(!watcher.update(root));
            std::this_thread::sleep_for(1ms);
        } while (Clock::now() < deadline);
    }
} // namespace

int main()
{
    static_assert(!std::is_copy_constructible_v<kh3ap::RequestFileWatcher>);
    static_assert(!std::is_copy_assignable_v<kh3ap::RequestFileWatcher>);

    const auto root = std::filesystem::current_path() /
                      ("request-file-watcher-" + std::to_string(GetCurrentProcessId()) + "-" +
                       std::to_string(GetTickCount64()));
    assert(!std::filesystem::exists(root));
    {
        kh3ap::RequestFileWatcher watcher{L"request.json", L"second.json"};
        assert(!watcher.update(std::nullopt));
        // Selecting a directory before the client creates it must not leave
        // the service permanently disconnected from future requests.
        assert(!watcher.update(root));
        assert(std::filesystem::create_directory(root));
        write_file(root / L"request.json", "first request");
        assert(await_change(watcher, root));
        assert_idle(watcher, root);
        assert(watcher.update(root, true));

        write_file(root / L"status.json", "ordinary status update");
        assert_idle(watcher, root);
        write_file(root / L"request.json", "a changed request with a different size");
        assert(await_change(watcher, root));
        assert_idle(watcher, root);
        assert(std::filesystem::remove(root / L"request.json"));
        assert(await_change(watcher, root));
        assert(!watcher.update(root, true));

        write_file(root / L"second.json", "second request");
        assert(await_change(watcher, root));
        assert(watcher.update(root, true));
        // Switching away releases the old subscription. Returning samples
        // already existing requests before waiting for another notification.
        assert(!watcher.update(std::nullopt));
        assert(watcher.update(root));
        assert(std::filesystem::remove(root / L"second.json"));
        assert(await_change(watcher, root));
        assert(!watcher.update(root, true));
        // A client may replace its bridge directory without changing the
        // configured path. The observer must reconnect to the new directory.
        assert(std::filesystem::remove(root / L"status.json"));
        assert(std::filesystem::remove(root));
        assert_idle(watcher, root);
        assert(std::filesystem::create_directory(root));
        write_file(root / L"request.json", "request after directory replacement");
        assert(await_change(watcher, root));
        assert(std::filesystem::remove(root / L"request.json"));
    }
    assert(std::filesystem::remove(root));
}
