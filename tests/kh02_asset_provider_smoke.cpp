// Explicit integration helper; never registered as an automatic game test.
#include "kh3ap/features/kh02/transport/kh02_companion_process.h"
#include "kh3ap/features/kh02/transport/kh02_remote_pipe_transport.h"
#include "kh3ap/features/kh02/transport/kh02_index_state.h"
#include "kh02_source_audit.h"
#include <iostream>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2 && argc != 3)
    {
        std::cerr << "Usage: kh02_asset_provider_smoke <verified-source-executable>\n";
        return 2;
    }
    try
    {
        kh3ap::Kh02CompanionProcess child;
        const auto session = child.start(argv[1]);
        child.wait_assets_ready({});
        kh3ap::Kh02RemotePipeTransport transport;
        transport.bind(session.identity.source_pid);
        transport.load_index();
        if (argc == 3 && std::wstring_view(argv[2]) == L"--audit")
        {
            const auto result = kh02test::audit(transport);
            transport.disable();
            child.stop(session.identity);
            std::cout << result.dump(2) << '\n';
            return result.at("status") == "passed" ? 0 : 1;
        }
        const auto& index = kh02asset::asset_index();
        if (index.files.size() < 20'000 || index.voice_pairs.empty())
        {
            throw std::runtime_error("incomplete mounted source index");
        }
        std::size_t checked{};
        for (uint32_t i = 0; i < index.files.size(); ++i)
        {
            const auto& file = index.files[i];
            if (file.suffix == L"/content/zlueprints/gimmick/dw/g_dw_thorn/g_dw_thornb.uasset" ||
                file.suffix == L"/content/zffects/level/dw/dwcom/gimmick/item/par/"
                               L"vg_dwex_cha_lst_par20.uexp" ||
                file.suffix == L"/content/zevels/dw/dw_02/umap/dw_02_camera_ms200.umap" ||
                file.suffix == L"/content/zutscene/localization/en/dw/cutscene_dw02_dw009.umap")
            {
                std::vector<uint8_t> bytes(static_cast<std::size_t>(file.size));
                if (!transport.read(i, 0, static_cast<uint32_t>(bytes.size()), bytes.data()))
                {
                    throw std::runtime_error("source compatibility read failed: " +
                                             transport.snapshot().adaptation_error);
                }
                ++checked;
            }
        }
        if (checked != 4)
        {
            throw std::runtime_error("source smoke packages missing");
        }
        const auto report = nlohmann::json{{"files", index.files.size()},
                                           {"voice_pairs", index.voice_pairs.size()},
                                           {"compatibility_packages", checked},
                                           {"source_build", index.source_build},
                                           {"status", "passed"}};
        transport.disable();
        child.stop(session.identity);
        std::cout << report.dump() << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
