from pathlib import Path
import re,sys
p=Path(__file__).resolve().parents[1]/"KH3APProbeCpp/kh3ap/features/enemies/titan_arena.cpp"
s=p.read_text();s=s[s.index("namespace kh3ap"):]
def replace_block(s,marker,replacement):
    start=s.index(marker); pos=s.index("{",start)+1;depth=1
    while depth:
        depth+=(s[pos]=="{")-(s[pos]=="}");pos+=1
    return s[:start]+replacement+s[pos:]
s=replace_block(s,"        bool bool_call(",'''        bool bool_call(UObject* object,const wchar_t* name,const wchar_t*,bool value) {
    ++fake_titan::calls;
    if(std::wstring_view(name)==L"GetActorEnableCollision") return object->collision;
    if(std::wstring_view(name)==L"SetActorEnableCollision") object->collision=value;
    if(std::wstring_view(name)==L"SetActorHiddenInGame") object->hidden=value;
    ++object->writes;return true;
}''')
s=replace_block(s,"        bool hidden_value(","        bool hidden_value(UObject* actor) { ++fake_titan::calls;return actor->hidden; }")
# This fixture exercises streamed arena geometry and binding notifications.
# The separate replacement-Titan attack hook needs the real executable/UE4SS
# ABI; it is neither called nor simulated by the arena event tests.
s=replace_block(s,"        bool has_titan_attack_gate_layout()", "")
s=replace_block(s,"    void install_replacement_titan_attack_support()", "")
s=s.replace("using Clock = std::chrono::steady_clock;","using Clock = fake_titan::Clock;")
Path(sys.argv[1]).write_text('#pragma once\n#include "titan_events_stubs.h"\n'+s)
