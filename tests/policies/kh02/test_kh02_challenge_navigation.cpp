#include "kh3ap/features/kh02/challenges/kh02_challenge_navigation.h"
#include <cassert>
#include <iostream>
using kh3ap::Kh02ChallengeControllerNavigation;
int main(){
    Kh02ChallengeControllerNavigation nav;
    assert(nav.accept(0,L"Gamepad_DPad_Down",1,0,0));
    assert(!nav.accept(0,L"Gamepad_LeftStick_Down",1,0,1));
    for(int t=1;t<350;++t){
        assert(!nav.accept(0,L"Gamepad_DPad_Down",1,2,t));
        assert(!nav.accept(0,L"Gamepad_LeftStick_Down",1,2,t));
    }
    assert(nav.poll(349)==0);
    assert(nav.poll(350)==1);
    for(int i=0;i<1000;++i)assert(!nav.accept(0,L"Gamepad_DPad_Down",1,2,350));
    assert(!nav.accept(0,L"Gamepad_DPad_Down",1,2,489));
    assert(nav.poll(489)==0);
    assert(nav.poll(490)==1);
    // One alias releasing cannot rearm the still-held direction.
    nav.accept(0,L"Gamepad_DPad_Down",1,1,500);
    assert(!nav.accept(0,L"Gamepad_DPad_Down",1,0,501));
    assert(!nav.accept(0,L"Gamepad_LeftStick_Down",1,2,501));
    // A stalled frame emits one repeat, with no catch-up steps.
    assert(nav.poll(5000)==1);
    assert(nav.poll(5000)==0);
    assert(!nav.accept(0,L"Gamepad_DPad_Down",1,2,5000));
    nav.reset();
    assert(nav.accept(0,L"Gamepad_LeftStick_Down",1,0,6000));
    nav.accept(0,L"Gamepad_LeftStick_Down",1,1,6010);
    assert(!nav.accept(0,L"Gamepad_LeftStick_Down",1,0,6020)); // Neutral jitter.
    assert(nav.poll(6349)==0&&nav.poll(6350)==1); // Original hold deadline survives jitter.
    nav.accept(0,L"Gamepad_LeftStick_Down",1,1,6360);
    assert(nav.accept(0,L"Gamepad_LeftStick_Down",1,0,6450)); // Deliberate tap.
    assert(nav.accept(0,L"Gamepad_DPad_Up",-1,0,6460)); // Immediate reversal.
    assert(nav.poll(6809)==0&&nav.poll(6810)==-1);
    nav.reset();
    assert(nav.accept(0,L"Gamepad_RightShoulder",8,0,7000));
    assert(!nav.accept(0,L"Gamepad_RightShoulder",8,2,8000));
    assert(nav.poll(8000)==0);
    nav.accept(0,L"Gamepad_RightShoulder",8,1,8010);
    assert(nav.accept(0,L"Gamepad_RightShoulder",8,0,8100));
    nav.reset();assert(!nav.accept(0,L"Gamepad_DPad_Down",1,2,9000));assert(nav.poll(10000)==0);
    // Ten seconds of held input at a normal frame cadence, including whole
    // seconds with no engine Repeat events and bursts when they resume.
    for(const auto* key:{L"Gamepad_DPad_Down",L"Gamepad_LeftStick_Down"}){
        nav.reset();assert(nav.accept(0,key,1,0,0));
        int steps=0,last=0,max_gap=0;
        for(int t=1;t<=10000;t+=17){
            if(t/1000%2==0)for(int i=0;i<8;++i)assert(!nav.accept(0,key,1,2,t));
            const int delta=nav.poll(t);
            if(delta){assert(delta==1);if(last)max_gap=std::max(max_gap,t-last);last=t;++steps;}
        }
        assert(steps>=60&&steps<=70&&max_gap<=153);
        nav.accept(0,key,1,1,10001);assert(nav.poll(20000)==0);
    }
    // Color adjustments must remain smooth for either keyboard or controller,
    // even when native Repeat events disappear for seconds and resume in bursts.
    for (const auto* key : {L"Left", L"Gamepad_DPad_Left", L"Gamepad_LeftStick_Left"})
    {
        nav.reset();
        assert(nav.accept(0, key, -1, 0, 0));
        int steps = 0;
        int last = 0;
        int max_gap = 0;
        for (int t = 1; t <= 10000; t += 17)
        {
            if (t / 2000 % 2 == 0)
            {
                for (int i = 0; i < 8; ++i)
                {
                    assert(!nav.accept(0, key, -1, 2, t));
                }
            }
            if (const int delta = nav.poll(t, 50))
            {
                assert(delta == -1);
                if (last)
                {
                    max_gap = std::max(max_gap, t - last);
                }
                last = t;
                ++steps;
            }
        }
        assert(steps >= 185 && steps <= 190 && max_gap <= 51);
        nav.accept(0, key, -1, 1, 10001);
        assert(nav.poll(20000, 50) == 0);
        assert(nav.accept(0, key, -1, 0, 20001));
        nav.reset(); // A submenu transition cancels held adjustments.
        assert(!nav.accept(0, key, -1, 2, 20002));
        assert(nav.poll(30000, 50) == 0);
    }
    std::cout<<"Frame-clock controller and keyboard holds, repeat gaps, alias coalescing, jitter, reversal and release checks passed\n";
}
