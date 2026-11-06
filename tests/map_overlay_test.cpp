#include "kh3ap/features/ui/map_overlay/map_overlay_math.h"
#include "kh3ap/features/ui/map_overlay/map_overlay_chest_schedule.h"
#include "kh3ap/features/ui/map_overlay/map_icon_options.h"
#include "kh3ap/features/ui/map_overlay/map_chest_policy.h"
#include "kh3ap/core/events/keyblade_inventory_events.h"
#include "kh3ap/features/treasure/chest_open_events.h"
#include <cassert>
#include <limits>
#include <stdexcept>
using namespace kh3ap;
int main() {
    // KH0.2 checks are outside the ordinary chest ledger. Their native openings
    // must still refresh the shared area-map/minimap completion snapshot.
    ChestOpenEvents openings;
    openings.configure({100});
    assert(!openings.pending());
    openings.write(4056);
    openings.write(4095);
    assert(openings.pending());
    auto opened=openings.take();
    assert(opened.changed && opened.checks.empty() && openings.events()==0);
    MapOverlayChestSchedule completion_schedule;
    completion_schedule.take({}); // Initial map snapshot.
    if(opened.changed)completion_schedule.changed();
    assert(completion_schedule.take({}).completion);
    assert(!completion_schedule.take({}).completion);
    assert(!openings.pending() && !openings.take().changed);
    openings.write(100);
    openings.write(100);
    openings.write(4072);
    opened=openings.take();
    assert(opened.changed && opened.checks==std::set<uint64_t>{100});
    assert(openings.events()==2 && !openings.pending());
    // Reconfiguration must not lose a map invalidation waiting to be drained.
    openings.write(4091);
    openings.configure({200});
    assert(openings.take().changed);
    assert(!openings.pending());

    chest_manifest::Snapshot palette{.process_id=42,.seed=L"test",.slot=1};
    palette.map_keyblades={{L"chest1",{L"Classic Tone",L"WEP_KEYBLADE_CLASSIC"}},
        {L"chest2",{L"Classic Tone",L"WEP_KEYBLADE_CLASSIC"}}};
    palette.eligible_keyblades={{L"Classic Tone",L"WEP_KEYBLADE_CLASSIC"},
        {L"Grand Chef",L"WEP_KEYBLADE_CHEF"},{L"Hunny Spout",L"WEP_KEYBLADE_HUNNY"}};
    assert(map_chest_palette_matches(&palette,42,L"test",1));
    assert(!map_chest_palette_matches(&palette,43,L"test",1));
    assert(!map_chest_palette_matches(&palette,42,L"other",1));
    assert(!map_chest_palette_matches(&palette,42,L"test",2));
    RuntimeKeybladeOwnershipCache ownership;
    bool owned=false, query_available=true;
    int queries=0;
    const auto owns=[&](const std::wstring& token) {
        try {
            return ownership.get(token,[&] {
                ++queries;
                if (!query_available) throw std::runtime_error("inventory unavailable");
                return owned && token==L"WEP_KEYBLADE_CLASSIC";
            }).owned;
        } catch (const std::exception&) { return false; }
    };
    const std::array<std::wstring_view,4> names{L"Classic Tone",L"Ever After",L"Grand Chef",L"Hunny Spout"};
    const std::vector<size_t> eligible_rows{0,2,3};
    assert(map_chest_alpha(&palette,L"chest1",true,owns)==0.70);
    assert(map_chest_is_locked(&palette,L"chest1",owns));
    assert(!map_chest_is_locked(&palette,L"melody_or_normal",owns));
    assert(!map_chest_is_locked(nullptr,L"chest1",owns));
    assert(map_chest_alpha(&palette,L"chest2",true,owns)==0.70 && queries==1);
    assert(map_chest_alpha(&palette,L"chest1",false,owns)==0.0 && queries==1);
    assert(map_chest_alpha(&palette,L"melody_or_normal",false,owns)==1.0 && queries==1);
    assert(map_chest_alpha(nullptr,L"chest1",false,owns)==1.0 && queries==1);
    assert(map_chest_legend_rows(&palette,names)==eligible_rows);
    owned=true;
    notify_runtime_keyblade_inventory_changed(L"WEP_KEYBLADE_CLASSIC");
    assert(map_chest_alpha(&palette,L"chest1",true,owns)==1.0);
    assert(!map_chest_is_locked(&palette,L"chest1",owns));
    assert(map_chest_alpha(&palette,L"chest2",false,owns)==1.0 && queries==2);
    assert((map_chest_owned_legend_rows(&palette,names,owns)==std::vector<size_t>{0}));
    ownership.clear();query_available=false;
    assert(map_chest_alpha(&palette,L"chest1",true,owns)==0.70);
    assert(map_chest_alpha(&palette,L"chest1",false,owns)==0.0);
    assert(map_chest_owned_legend_rows(&palette,names,owns).empty());
    query_available=true;
    assert(map_chest_alpha(&palette,L"chest1",true,owns)==1.0); // Failed queries retry.
    ownership.clear();owned=false;
    assert(map_chest_alpha(&palette,L"chest2",true,owns)==0.70);
    assert(map_chest_alpha(&palette,L"chest2",false,owns)==0.0);
    assert(map_chest_legend_rows(&palette,names)==eligible_rows);
    palette.eligible_keyblades.clear();palette.map_keyblades.clear();
    assert(map_chest_legend_rows(&palette,names).empty());
    assert(map_chest_legend_rows(nullptr,names).empty());
    assert(map_chest_owned_legend_rows(nullptr,names,owns).empty());
    assert(map_chest_alpha(&palette,L"chest1",false,owns)==1.0); // Chestsanity off.
    assert(!map_chest_is_locked(&palette,L"chest1",owns));
    assert(map_chest_legend_height(15)==486 && map_chest_legend_height(3)==150);
    // Real 2194x1234 stage: scrollRect rounds 87.76 to 88. That must not
    // suppress opening; displaced layers and incorrect scales must still fail.
    assert(map_overlay_layout_matches(88,87.76));
    assert(map_overlay_layout_matches(62,61.7));
    assert(map_overlay_layout_matches(1110,1110.6));
    assert(!map_overlay_layout_matches(0,87.76));
    assert(!map_overlay_layout_matches(85,87.76));
    assert(map_overlay_layout_matches(1.08457,1.0845703125,true));
    assert(!map_overlay_layout_matches(1,1.0845703125,true));
    assert(!map_overlay_layout_matches(std::numeric_limits<double>::quiet_NaN(),88));
    // The legend floats within the centered map viewport without reserving a
    // column. Its row count must not affect the map's fit or focus position.
    for (const auto [sw,sh]:{std::pair{1280.,720.},std::pair{1920.,1080.},
            std::pair{3840.,2160.},std::pair{2560.,1080.},std::pair{1280.,1024.}}) {
        for (const auto [iw,ih]:{std::pair{512.,512.},std::pair{1024.,256.},std::pair{256.,1024.}}) {
            const auto keyed=map_overlay_layout(sw,sh,iw,ih,238,486);
            assert(std::abs(keyed.map_x-(sw-iw*keyed.fit)/2)<.001);
            assert(keyed.fit>std::min(sw*.72/iw,sh*.76/ih));
            assert(keyed.map_x>=sw*.09-.001 && keyed.map_y>=sh*.05-.001);
            assert(keyed.map_y+ih*keyed.fit<=sh*.95+.001);
            assert(keyed.map_x+iw*keyed.fit<=sw*.91+.001);
            assert(keyed.legend_x>=keyed.view_x+keyed.view_width/2);
            assert(keyed.legend_x+238*keyed.legend_scale<=sw*.86+.001);
            assert(keyed.legend_y+486*keyed.legend_scale<=sh*.91);
            assert(keyed.legend_y>=sh*.09+40*keyed.legend_scale);
            for (double zoom:{.5,.75,1.,1.25,1.5,2.}) {
                for (const auto focus:{MapOverlayPoint{0,0,0},MapOverlayPoint{iw/2,ih/2,0},MapOverlayPoint{iw,ih,0}}) {
                    const auto magnified=map_overlay_layout(sw,sh,iw,ih,238,486,zoom,focus);
                    const auto compact=map_overlay_layout(sw,sh,iw,ih,238,150,zoom,focus);
                    assert(magnified.fit==compact.fit && magnified.map_x==compact.map_x && magnified.map_y==compact.map_y);
                    assert(magnified.legend_x==keyed.legend_x && magnified.legend_y==keyed.legend_y);
                    assert(magnified.legend_scale==keyed.legend_scale);
                    assert(magnified.view_x+magnified.view_width>=keyed.legend_x+238*keyed.legend_scale);
                    assert(std::abs(magnified.view_x+magnified.view_width/2-sw/2)<.001);
                    assert(std::abs(magnified.fit/keyed.fit-zoom)<.001);
                    // At the texture edges, Sora stays within the viewport and
                    // the texture never pans farther than an edge can cover.
                    const auto x=magnified.map_x+focus.x*magnified.fit;
                    const auto y=magnified.map_y+focus.y*magnified.fit;
                    assert(x>=magnified.view_x-.001 && x<=magnified.view_x+magnified.view_width+.001);
                    assert(y>=magnified.view_y-.001 && y<=magnified.view_y+magnified.view_height+.001);
                    if (ih*magnified.fit>magnified.view_height) {
                        assert(magnified.map_y<=magnified.view_y+.001);
                        assert(magnified.map_y+ih*magnified.fit>=magnified.view_y+magnified.view_height-.001);
                    }
                }
            }
        }
    }
    // San Fransokyo's tall texture gains about 18% with the same legend size.
    const auto bx=map_overlay_layout(1920,1080,512,1024,238,486);
    assert(bx.fit/(1080*.76/1024)>1.18);
    assert(bx.legend_scale==1);
    MapOverlayZoom zoom;
    assert(zoom.chest_factor()==1);
    bool zoom_changed{};
    assert(!zoom.route(0,1,0,false,zoom_changed) && !zoom_changed); // normal R2 while closed
    assert(!zoom.route(0,-1,0,false,zoom_changed) && !zoom_changed); // normal L2 while closed
    assert(!zoom.route(0,1,2,true,zoom_changed)); // held before opening: no capture
    assert(zoom.route(0,1,0,true,zoom_changed) && zoom_changed && zoom.factor()==1.25);
    assert(zoom.chest_factor()==1.125);
    assert(zoom.route(0,1,2,true,zoom_changed) && !zoom_changed); // no repeat zoom
    assert(zoom.route(0,-1,0,true,zoom_changed) && zoom_changed && zoom.factor()==1);
    assert(zoom.route(1,1,0,true,zoom_changed) && zoom_changed); // second controller
    assert(zoom.route(0,1,1,false,zoom_changed) && !zoom_changed); // owned releases after close
    assert(zoom.route(0,-1,1,false,zoom_changed) && !zoom_changed);
    assert(zoom.route(1,1,1,false,zoom_changed) && !zoom_changed);
    assert(!zoom.route(0,1,0,false,zoom_changed));
    for (int direction:{1,-1}) {
        for (int i=0;i<16;++i) {
            assert(zoom.route(0,direction,0,true,zoom_changed));
            assert(zoom.factor()>=.5 && zoom.factor()<=2);
            assert(zoom.route(0,direction,1,true,zoom_changed));
        }
        assert(zoom.factor()==(direction==1?2:.5));
        assert(zoom.chest_factor()==(direction==1?1.5:.75));
        assert(zoom.route(0,direction,0,true,zoom_changed) && !zoom_changed); // still consumes at limit
        assert(zoom.route(0,direction,1,true,zoom_changed));
    }
    for (auto option:{MapIconOption::Chests,MapIconOption::Emblems,MapIconOption::Ingredients,
                     MapIconOption::Minimap,MapIconOption::LargeMap})
        assert(map_icon_enabled(map_icon_defaults,option));
    const auto mini_off=map_icon_defaults & ~static_cast<uint32_t>(MapIconOption::Minimap);
    assert(!map_icon_enabled(mini_off,MapIconOption::Minimap));
    assert(map_icon_enabled(mini_off,MapIconOption::LargeMap));
    assert(map_icon_enabled(mini_off,MapIconOption::Chests));
    MapTriggerInput triggers;
    int trigger_zoom=0;bool trigger_toggle=false;
    for(int first:{-1,1}) {
        assert(!triggers.route(0,first,0,false,trigger_zoom,trigger_toggle));
        assert(triggers.route(0,first,0,true,trigger_zoom,trigger_toggle) && !trigger_zoom && !trigger_toggle);
        assert(triggers.route(0,first,2,true,trigger_zoom,trigger_toggle) && !trigger_zoom && !trigger_toggle);
        assert(triggers.route(0,-first,0,true,trigger_zoom,trigger_toggle) && trigger_toggle && !trigger_zoom);
        assert(triggers.route(0,-first,0,true,trigger_zoom,trigger_toggle) && !trigger_toggle);
        assert(triggers.route(0,first,1,true,trigger_zoom,trigger_toggle) && !trigger_zoom);
        assert(triggers.route(0,first,0,true,trigger_zoom,trigger_toggle) && !trigger_toggle);
        assert(triggers.route(0,first,1,true,trigger_zoom,trigger_toggle) && !trigger_zoom);
        assert(triggers.route(0,-first,1,true,trigger_zoom,trigger_toggle) && !trigger_zoom);
        assert(triggers.route(0,first,0,true,trigger_zoom,trigger_toggle));
        assert(triggers.route(0,first,1,true,trigger_zoom,trigger_toggle) && trigger_zoom==first);
    }
    assert(triggers.route(0,-1,0,true,trigger_zoom,trigger_toggle));
    assert(triggers.route(1,1,0,true,trigger_zoom,trigger_toggle) && !trigger_toggle);
    assert(triggers.route(0,-1,1,true,trigger_zoom,trigger_toggle) && trigger_zoom==-1);
    assert(triggers.route(1,1,1,true,trigger_zoom,trigger_toggle) && trigger_zoom==1);
    assert(triggers.route(0,-1,0,true,trigger_zoom,trigger_toggle));
    triggers.cancel_pending(); // Map/menu closure cancels delayed zoom.
    assert(triggers.route(0,1,0,true,trigger_zoom,trigger_toggle) && !trigger_toggle);
    assert(triggers.route(0,-1,1,false,trigger_zoom,trigger_toggle) && !trigger_zoom);
    assert(triggers.route(0,1,1,true,trigger_zoom,trigger_toggle) && !trigger_zoom);
    assert(!triggers.route(0,1,2,true,trigger_zoom,trigger_toggle)); // Pre-held input is not captured.
    assert(map_icon_enabled(map_icon_defaults,MapIconOption::LockedChests));
    MapIconInput icon_input;
    bool icon_toggle{};
    assert(!icon_input.route(0,0,false,icon_toggle) && !icon_toggle);
    assert(icon_input.route(0,0,true,icon_toggle) && icon_toggle);
    assert(icon_input.route(0,2,true,icon_toggle) && !icon_toggle);
    assert(icon_input.route(0,0,true,icon_toggle) && !icon_toggle);
    assert(icon_input.route(0,1,false,icon_toggle) && !icon_toggle); // release after menu opens
    assert(!icon_input.route(0,1,true,icon_toggle) && !icon_toggle);
    assert(icon_input.route(0,0,true,icon_toggle) && icon_toggle);
    assert(icon_input.route(1,0,true,icon_toggle) && icon_toggle); // controllers are independent
    assert(icon_input.route(0,1,true,icon_toggle) && !icon_toggle);
    assert(icon_input.route(1,1,true,icon_toggle) && !icon_toggle);
    MinimapIconProjection mini{{0,1,-1,0,150,150},300,300,1600,50,240,240};
    auto mini_point=mini.project(20,40,10);
    assert(mini_point && std::abs(mini_point->x-1688)<0.001 && std::abs(mini_point->y-186)<0.001);
    assert(!mini.project(0,150,10)); // rotated point reaches the clipped left edge
    assert(!mini.project(200,0,10)); // outside the native viewport
    mini.canvas.tx=std::numeric_limits<double>::quiet_NaN();
    assert(!mini.project(0,0,10));
    using namespace std::chrono;
    MapOverlayChestSchedule schedule;
    const auto start=MapOverlayChestSchedule::Clock::time_point{seconds(100)};
    auto work=schedule.take(start);
    assert(work.discover_other && work.completion);
    work=schedule.take(start+milliseconds(500));
    assert(!work.discover_other && !work.completion); // idle display never rereads completion
    schedule.changed(); schedule.changed(); // coalesced chest-open notifications
    work=schedule.take(start+milliseconds(600));
    assert(!work.discover_other && work.completion); // events do not enumerate the world
    work=schedule.take(start+milliseconds(1000));
    assert(!work.discover_other && !work.completion);
    work=schedule.take(start+milliseconds(1100));
    assert(!work.discover_other && !work.completion); // no settling timer
    schedule.changed(); // native completion can arrive later than the old 500ms deadline
    work=schedule.take(start+milliseconds(1600));
    assert(!work.discover_other && work.completion);
    work=schedule.take(start+seconds(5));
    assert(work.discover_other && !work.completion); // other collectibles cannot poll chests
    schedule={}; // new map or reopen bootstraps immediately, even after a long pause
    work=schedule.take(start+seconds(6));
    assert(work.discover_other && work.completion);
    schedule={};
    int discoveries=0,completion_reads=0;
    for (int tick=0;tick<120;++tick) {
        work=schedule.take(start+milliseconds(500*tick));
        discoveries+=work.discover_other;
        completion_reads+=work.completion;
    }
    assert(discoveries==12 && completion_reads==1); // only the initial chest snapshot
    schedule.changed();
    work=schedule.take(start+seconds(600)); // missed events / long hidden interval
    assert(work.discover_other && work.completion);
    work=schedule.take(start+seconds(3600));
    assert(work.discover_other && !work.completion);

    // Loaded Arendelle metadata; negative world coordinates and non-square areas.
    const MapOverlayArea area{0.006667,-30000,30000};
    auto chest=map_overlay_world_point(area,-20000,67500);
    assert(chest && std::abs(chest->x-66.67)<1e-8 && std::abs(chest->y-250.0125)<1e-8);
    assert(map_overlay_contains(*chest,512,256));
    assert(!map_overlay_contains(*chest,512,128));
    assert(!map_overlay_contains({-1,20,0},512,512));
    assert(!map_overlay_contains({512,20,0},512,512));
    assert(!map_overlay_world_point({0,0,0},1,1));
    assert(!map_overlay_world_point(area,std::numeric_limits<double>::quiet_NaN(),1));
    auto p=map_overlay_point(0,0,35,-200,-100,0.5,0.5,0);
    assert(p && std::abs(p->x-400)<1e-8 && std::abs(p->y-200)<1e-8 && p->heading==35);
    // A ninety-degree rotating minimap must resolve to the same texture point.
    p=map_overlay_point(0,0,125,100,-200,0.5,0.5,90);
    assert(p && std::abs(p->x-400)<1e-8 && std::abs(p->y-200)<1e-8 && p->heading==35);
    assert(!map_overlay_point(0,0,0,0,0,0,1,0));
    assert(!map_overlay_point(0,0,0,0,0,1,1,std::numeric_limits<double>::quiet_NaN()));
    // Landmark layer translation, rotation and nonuniform scale round-trip.
    auto landmark=map_overlay_canvas_point(30,70,200,-100,2,0.5,90);
    assert(landmark && std::abs(landmark->x-165)<1e-8 && std::abs(landmark->y+40)<1e-8);
    auto local=map_overlay_point(landmark->x,landmark->y,90,200,-100,2,0.5,90);
    assert(local && std::abs(local->x-30)<1e-8 && std::abs(local->y-70)<1e-8);
    assert(!map_overlay_canvas_point(1,2,0,0,0,1,0));
    assert(!map_overlay_canvas_point(1,2,0,0,1,1,std::numeric_limits<double>::infinity()));
    MapOverlayInput input;
    bool toggle=false;
    auto press=[&](int controller,bool keyboard,int event,bool eligible,bool opened,int ms) {
        return input.route(controller,keyboard,event,eligible,opened,start+milliseconds(ms),toggle);
    };
    assert(!press(0,false,0,false,false,0) && !toggle); // menu input is untouched
    assert(press(0,false,0,true,false,0) && !toggle);
    assert(!input.advance(start+milliseconds(249),true,false));
    assert(press(0,false,1,true,false,249) && !toggle); // a short tap never opens
    assert(!input.pending() && !input.advance(start+seconds(1),true,false));
    assert(press(0,false,0,true,false,1000) && !toggle);
    assert(press(0,false,2,true,false,1100) && !toggle); // repeat cannot restart deadline
    assert(press(0,false,0,true,false,1200) && !toggle); // duplicate press
    assert(input.advance(start+milliseconds(1250),true,false)); // opens while still held
    assert(!input.advance(start+seconds(2),true,true)); // no second toggle
    assert(press(0,false,1,true,true,2000) && !toggle);
    assert(press(0,false,0,true,true,2100) && toggle); // closing is still instant
    assert(press(0,false,1,false,false,2200) && !toggle); // owned release through menu
    assert(!press(0,false,1,true,false,2300) && !toggle);
    assert(press(1,false,0,true,false,3000) && !toggle);
    assert(press(1,false,1,true,false,3250) && toggle); // exact threshold between ticks
    assert(press(0,false,0,true,false,4000) && !toggle);
    assert(!input.advance(start+milliseconds(4100),false,false)); // menu/pawn loss cancels
    assert(!input.advance(start+milliseconds(4500),true,false)); // cannot resume old hold
    assert(press(0,false,1,true,false,4500) && !toggle);
    assert(press(0,false,0,true,false,5000) && !toggle);
    assert(press(0,true,0,true,false,5100) && toggle); // F8 remains immediate, cancels L3
    assert(!input.pending());
    assert(press(0,false,1,true,false,5500) && !toggle);
    // Turning the shortcut off drains an owned press without opening again,
    // then lets new presses pass through until the shortcut is enabled.
    assert(press(0,true,2,false,false,6000) && !toggle);
    assert(press(0,true,1,false,false,6100) && !toggle);
    assert(!press(0,true,0,false,false,6200) && !toggle);
    assert(!press(0,true,1,false,false,6300) && !toggle);
    assert(press(0,true,0,true,false,6400) && toggle);
    assert(press(0,false,0,true,false,7000) && !toggle);
    input.cancel_open(); // disabled option: drain release even after re-enabling
    assert(press(0,false,1,true,false,7300) && !toggle);
}
