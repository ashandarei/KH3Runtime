#include "kh3ap/features/treasure/music_chest/mom_audio.h"
#include "kh3ap/features/treasure/music_chest/mom_audio_state.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace kh3ap {
namespace {
        constexpr auto audio_session_recheck_interval = std::chrono::seconds(1);

using Microsoft::WRL::ComPtr;

bool mute_process_sessions(RuntimeMoMAudioState& state) {
    ComPtr<IMMDeviceEnumerator> devices;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&devices)))) return false;
    ComPtr<IMMDeviceCollection> endpoints;
    if (FAILED(devices->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &endpoints))) return false;
    UINT count{};
    if (FAILED(endpoints->GetCount(&count))) return false;
    bool found = false, success = true;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<IAudioSessionEnumerator> sessions;
        int session_count{};
        if (FAILED(endpoints->Item(i, &device)) ||
            FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
                nullptr, reinterpret_cast<void**>(manager.GetAddressOf()))) ||
            FAILED(manager->GetSessionEnumerator(&sessions)) ||
            FAILED(sessions->GetCount(&session_count))) { success = false; continue; }
        for (int j = 0; j < session_count; ++j) {
            ComPtr<IAudioSessionControl> control;
            ComPtr<IAudioSessionControl2> identity;
            ComPtr<ISimpleAudioVolume> volume;
            DWORD pid{};
            if (FAILED(sessions->GetSession(j, &control)) || FAILED(control.As(&identity))) continue;
            // S_FALSE denotes a cross-process session; do not mute other apps.
            if (identity->GetProcessId(&pid) != S_OK || pid != GetCurrentProcessId()) continue;
            AudioSessionState status{};
            if (FAILED(control->GetState(&status)) || status == AudioSessionStateExpired) continue;
            LPWSTR instance{};
            if (FAILED(identity->GetSessionInstanceIdentifier(&instance)) || !instance) { success = false; continue; }
            const std::wstring id(instance);
            CoTaskMemFree(instance);
            if (FAILED(control.As(&volume))) { success = false; continue; }
            found = true;
            success = state.mute(id, {
                [volume]() -> std::optional<bool> {
                    BOOL muted{};
                    if (FAILED(volume->GetMute(&muted))) return std::nullopt;
                    return muted != FALSE;
                },
                [volume, control](bool muted) {
                    BOOL actual{};
                    if (SUCCEEDED(volume->SetMute(muted, nullptr)) &&
                        SUCCEEDED(volume->GetMute(&actual)) && (actual != FALSE) == muted) return true;
                    // Try restoration even on an expired session: Windows may
                    // retain its mixer setting for the next stream/process.
                    AudioSessionState current{};
                    return SUCCEEDED(control->GetState(&current)) && current == AudioSessionStateExpired;
                }
            }) && success;
        }
    }
    return found && success;
}

}
struct RuntimeMoMAudio::Impl {
public:
    explicit Impl(std::function<void(const wchar_t*)> report) : report_(std::move(report)) {}
    ~Impl() {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        changed_.notify_one();
        if (thread_.joinable()) thread_.join();
    }
    void request(bool muted) {
        std::lock_guard lock(mutex_);
        if (!thread_.joinable()) {
            if (!muted) return;
            thread_ = std::thread([this] { run(); });
        }
        requested_ = muted;
        ++revision_;
        changed_.notify_one();
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread thread_;
    bool requested_{}, stopping_{};
    unsigned revision_{};
    std::function<void(const wchar_t*)> report_;

    void run() {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialized)) {
            if (report_) report_(L"MoM audio: COM initialization failed.");
            return;
        }
        {
            RuntimeMoMAudioState state;
            std::unique_lock lock(mutex_);
            bool warned = false;
            while (!stopping_) {
                const auto revision = revision_;
                const bool muted = requested_;
                lock.unlock();
                bool success = false;
                try { success = muted ? mute_process_sessions(state) : state.restore(); }
                catch (...) { /* Retry without losing captured mute states. */ }
                if (!success && !warned && report_)
                    report_(L"MoM audio handoff pending; retrying audio sessions.");
                warned = !success;
                lock.lock();
                const auto changed = [&] { return stopping_ || revision_ != revision; };
                // Discover replacement devices/streams while muted, and retry
                // failed restores. With no handoff or pending restore, sleep.
                if (muted || !state.empty()) changed_.wait_for(lock, audio_session_recheck_interval, changed);
                else changed_.wait(lock, changed);
            }
            lock.unlock();
            try { (void)state.restore(); } catch (...) {}
        } // Release COM session interfaces before leaving the apartment.
        CoUninitialize();
    }
};
RuntimeMoMAudio::RuntimeMoMAudio(std::function<void(const wchar_t*)> report)
    : impl_(std::make_unique<Impl>(std::move(report))) {}
RuntimeMoMAudio::~RuntimeMoMAudio() = default;
void RuntimeMoMAudio::set_muted(bool muted) { impl_->request(muted); }
}
