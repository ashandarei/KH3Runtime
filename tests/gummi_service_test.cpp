#include "gummi_service_under_test.h"

#include <cassert>
#include <iostream>

using namespace kh3ap;

int main()
{
    const auto now = std::chrono::steady_clock::now();
    {
        TestColorGummi service;
        UObject material;
        material.serial = 0;
        Ship ship;
        ship.plan = L"prefab";
        service.paint_slot(ship, &material, 0, L"seed", RuntimeColorHarmonyMode::Coordinated);
        assert(material.palette_use == 0 && material.material_writes == 2);
        // Recycle the object address and object-array slot between frames.
        ++material.serial;
        material.material_writes = 0;
        material.palette_use = 1;
        material.palette_index = 42;
        material.custom_color = {0.9f, 0.8f, 0.7f, 0.6f};
        assert(service.restore_all() == 0 && material.material_writes == 0);
        service.paint_slot(ship, &material, 0, L"seed", RuntimeColorHarmonyMode::Coordinated);
        assert(service.originals.at(&material).palette_index == 42);
        assert(service.restore_all() == 1);
        const std::array<float, 4> expected{0.9f, 0.8f, 0.7f, 0.6f};
        assert(material.custom_color == expected && material.palette_use == 1);
        ++material.serial;
        service.reset_level();
        assert(service.originals.empty());
    }
    {
        TestColorGummi service;
        UObject actor;
        service.tick(L"seed", RuntimeColorHarmonyMode::Coordinated, now);
        service.plans[111] = L"other_prefab";
        service.track(&actor);
        service.tick(L"seed", RuntimeColorHarmonyMode::Coordinated, now);
        service.tick(L"seed", RuntimeColorHarmonyMode::Coordinated, now + std::chrono::milliseconds(301));
        assert(service.ships[0].status == ShipStatus::Custom && actor.material_writes == 0);
        // The incremental scan finds the matching table after classification.
        service.plans[actor.fingerprint] = L"matching_prefab";
        service.tick(L"seed", RuntimeColorHarmonyMode::Coordinated, now + std::chrono::seconds(1));
        assert(service.ships[0].status == ShipStatus::Done && actor.material_writes == 2);
        service.tick(L"seed", RuntimeColorHarmonyMode::Coordinated, now + std::chrono::seconds(2));
        assert(actor.material_writes == 2);
    }
    {
        TestColorGummi service;
        UObject custom;
        service.tick(L"seed", RuntimeColorHarmonyMode::Coordinated, now);
        service.plans[111] = L"other_prefab";
        service.track(&custom);
        service.tick(L"seed", RuntimeColorHarmonyMode::Coordinated, now);
        service.tick(L"seed", RuntimeColorHarmonyMode::Coordinated, now + std::chrono::milliseconds(301));
        service.plans[333] = L"another_prefab";
        service.tick(L"seed", RuntimeColorHarmonyMode::Coordinated, now + std::chrono::seconds(1));
        assert(service.ships[0].status == ShipStatus::Custom && custom.material_writes == 0);
    }
    {
        TestColorGummi service;
        UObject ship,table,editor;
        table.fingerprint=ship.fingerprint;table.name=L"late_prefab";
        fake_runtime::gummi_objects={};
        fake_runtime::gummi_objects.ships={&ship};
        fake_runtime::gummi_objects.editors={&editor};
        fake_runtime::gummi_objects.unfinished=true;
        ++fake_runtime::gummi_revision;
        service.tick(L"seed",RuntimeColorHarmonyMode::Coordinated,now);
        assert(service.ships.size()==1&&service.discovery_pending);
        const auto count=fake_runtime::gummi_snapshots;
        service.tick(L"seed",RuntimeColorHarmonyMode::Coordinated,now+std::chrono::milliseconds(99));
        assert(fake_runtime::gummi_snapshots==count);
        fake_runtime::gummi_objects.unfinished=false;
        fake_runtime::gummi_objects.tables={&table};
        service.tick(L"seed",RuntimeColorHarmonyMode::Coordinated,now+std::chrono::milliseconds(101));
        assert(!service.discovery_pending&&fake_runtime::table_reads==1);
        service.tick(L"seed",RuntimeColorHarmonyMode::Coordinated,now+std::chrono::milliseconds(401));
        assert(service.ships[0].status==ShipStatus::Done&&ship.material_writes==2);
        const auto reads=fake_runtime::world_reads,snapshots=fake_runtime::gummi_snapshots;
        for(int i=0;i<10000;++i)
            service.tick(L"seed",RuntimeColorHarmonyMode::Coordinated,now+std::chrono::seconds(i+1));
        assert(fake_runtime::world_reads==reads&&fake_runtime::gummi_snapshots==snapshots&&fake_runtime::table_reads==1);
        // A map event wakes independently of constructor events; tables are not reread.
        ++world_context_revision;
        service.tick(L"seed",RuntimeColorHarmonyMode::Coordinated,now+std::chrono::seconds(10002));
        assert(fake_runtime::gummi_snapshots==snapshots+1&&fake_runtime::table_reads==1);
        // Late construction is discovered immediately, including a protected editor ship.
        UObject late;
        fake_runtime::editing=&late;
        fake_runtime::gummi_objects.ships.push_back(&late);++fake_runtime::gummi_revision;
        service.tick(L"seed",RuntimeColorHarmonyMode::Coordinated,now+std::chrono::seconds(10003));
        assert(service.ships.size()==2&&late.material_writes==0);
        service.tick(L"seed",RuntimeColorHarmonyMode::Coordinated,now+std::chrono::seconds(10004));
        assert(late.material_writes==0);
        fake_runtime::editing=nullptr;
        service.tick(L"seed",RuntimeColorHarmonyMode::Coordinated,now+std::chrono::seconds(10005));
        service.tick(L"seed",RuntimeColorHarmonyMode::Coordinated,now+std::chrono::seconds(10006));
        assert(late.material_writes==2);
        assert(service.restore_all()==2&&ship.palette_use==1&&late.palette_use==1);
        fake_runtime::gummi_world.name=L"fz_01";++world_context_revision;
        service.tick(L"seed",RuntimeColorHarmonyMode::Coordinated,now+std::chrono::seconds(10007));
        assert(!service.active&&!service.discovery_pending);
        fake_runtime::gummi_world_available=false;++world_context_revision;
        service.tick(L"seed",RuntimeColorHarmonyMode::Coordinated,now+std::chrono::seconds(10008));
        assert(!service.active&&!service.discovery_pending);
        const auto absent=fake_runtime::gummi_snapshots;
        service.tick(L"seed",RuntimeColorHarmonyMode::Coordinated,now+std::chrono::seconds(10009));
        assert(fake_runtime::gummi_snapshots==absent);
        fake_runtime::gummi_world_available=true;fake_runtime::gummi_world.name=L"wm_01";++world_context_revision;
        service.tick(L"seed",RuntimeColorHarmonyMode::Coordinated,now+std::chrono::seconds(10010));
        assert(service.active);
        fake_runtime::gummi_objects={};
    }
    std::cout << "Gummi event discovery, quiet frames, editor exclusion, lifetime and late-plan regression scenarios passed\n";
}
