#include "kh3ap/features/treasure/music_chest/mom_audio.h"
#define NOMINMAX
#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;

struct SilentSession {
    ComPtr<IAudioClient> client;
    ComPtr<ISimpleAudioVolume> volume;
    explicit SilentSession(IMMDevice* device) {
        assert(SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(client.GetAddressOf()))));
        WAVEFORMATEX* format{};
        assert(SUCCEEDED(client->GetMixFormat(&format)));
        GUID id{};
        assert(SUCCEEDED(CoCreateGuid(&id)));
        const auto result = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_NOPERSIST, 10000000, 0, format, &id);
        CoTaskMemFree(format);
        assert(SUCCEEDED(result));
        assert(SUCCEEDED(client->GetService(IID_PPV_ARGS(&volume))));
        ComPtr<IAudioRenderClient> render;
        assert(SUCCEEDED(client->GetService(IID_PPV_ARGS(&render))));
        UINT32 frames{};
        assert(SUCCEEDED(client->GetBufferSize(&frames)));
        BYTE* data{};
        assert(SUCCEEDED(render->GetBuffer(frames, &data)));
        assert(SUCCEEDED(render->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT)));
        assert(SUCCEEDED(volume->SetMute(FALSE, nullptr)));
        assert(SUCCEEDED(volume->SetMasterVolume(0.37f, nullptr)));
        assert(SUCCEEDED(client->Start()));
    }
    ~SilentSession() { if (client) client->Stop(); }
    bool muted() {
        BOOL value{};
        assert(SUCCEEDED(volume->GetMute(&value)));
        return value != FALSE;
    }
    void verify_volume() {
        float value{};
        assert(SUCCEEDED(volume->GetMasterVolume(&value)));
        assert(std::abs(value - 0.37f) < 0.0001f);
    }
};

template<class Predicate> void wait_for(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(10ms);
    assert(predicate());
}

int main() {
    assert(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
    {
        ComPtr<IMMDeviceEnumerator> devices;
        assert(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&devices))));
        ComPtr<IMMDevice> device;
        if (FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
            std::cout << "No render device; live audio integration skipped\n";
            return 77;
        }
        SilentSession first(device.Get()), initially_muted(device.Get());
        assert(SUCCEEDED(initially_muted.volume->SetMute(TRUE, nullptr)));
        {
            kh3ap::RuntimeMoMAudio audio;
            audio.set_muted(true);
            wait_for([&] { return first.muted(); });
            first.verify_volume();
            // Sessions created after the handoff (e.g. an output-device change)
            // must also be captured, muted, and restored independently.
            SilentSession replacement(device.Get());
            wait_for([&] { return replacement.muted(); });
            audio.set_muted(false);
            wait_for([&] { return !first.muted() && !replacement.muted(); });
            assert(initially_muted.muted());
            replacement.verify_volume();
            audio.set_muted(true);
            wait_for([&] { return first.muted() && replacement.muted(); });
        } // A service shutdown restores audio even during an active handoff.
        assert(!first.muted() && initially_muted.muted());
        first.verify_volume();
        initially_muted.verify_volume();
    }
    CoUninitialize();
    std::cout << "Live Windows audio mute/restore, new session, shutdown and volume tests passed\n";
}
