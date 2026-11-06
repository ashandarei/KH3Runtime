"""Compile production dispatch methods with deterministic engine/IO fakes."""
from pathlib import Path
import re
import sys
root=Path(__file__).parent/'../KH3APProbeCpp/kh3ap'
specs={
    'workshop':('features/moogle_shop/workshop_service.cpp','void tick()'),
    'party':('features/party/party_reconciliation.cpp','auto RuntimePartyMemberService::tick_maintenance('),
    'color_discovery':('features/colors/service/discovery.cpp',r'auto\s+RuntimeColorService::Impl::discover_pawns\('),
    'color_weapons':('features/colors/service/links.cpp',r'auto\s+RuntimeColorService::Impl::color_equipped_weapons\('),
    'coordinates':('features/journal/ingredient_journal_map_service.cpp','auto cache_package('),
    'journal_level_actors':('features/journal/ingredient_journal_map_service.cpp','auto authored_level_actors('),
}
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
for name,(path,signature) in specs.items():
    source=(root/path).read_text(encoding='utf-8')
    a=re.search(signature,source).start() if name.startswith('color_') else source.index(signature)
    b=source.index('{',a)+1;depth=1
    while depth:
        depth+=(source[b]=='{')-(source[b]=='}');b+=1
    method=source[a:b].replace('RuntimePartyMemberService::','').replace('RuntimeColorService::Impl::','').replace('std::chrono::steady_clock::now()','TestClock::now()')
    # These fragments are included inside test structs. Carry over the named
    # production constants referenced by the method as static members.
    constants = ''.join(
        f'static constexpr auto {key} = {value};\n'
        for key, value in re.findall(r'^\s*constexpr (?:auto|int|uint64_t) (\w+) = ([^;\n]+);', source[:a], re.M)
        if re.search(r'\b' + key + r'\b', method)
    )
    (out/f'{name}_remaining_under_test.h').write_text(constants+method,encoding='utf-8')
