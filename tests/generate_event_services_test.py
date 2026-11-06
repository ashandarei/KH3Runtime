"""Extract actual service dispatch methods; fake only clock, engine and IO."""
from pathlib import Path
import re
import sys
root=Path(__file__).parent/'../KH3APProbeCpp/kh3ap'
specs={
 'beat':('features/story/minigames/beat_of_lead_service.cpp',['tick','reset']),
 'union':('features/story/minigames/union_cross_command_service.cpp',['tick','reset']),
 'journal':('features/journal/ingredient_journal_service.cpp',['tick']),
 'emblem':('features/player/emblems/lucky_emblem_tracker_service.cpp',['tick','poll_components','reset_map_lifecycle','reset_for_bridge']),
 'guardians':('features/story/progression/story_progress_service.cpp',['process_guardians_of_light_skip']),
}
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
for label,(path,names) in specs.items():
 source=(root/path).read_text();methods=[]
 for name in names:
  start=source.index('        auto '+name+'(') if label!='journal' else source.index('    auto '+name+'(')
  end=source.index('{',start)+1;depth=1
  while depth:
   depth+=(source[end]=='{')-(source[end]=='}');end+=1
  methods.append(source[start:end].replace('std::chrono::steady_clock::now()','TestClock::now()'))
 extracted = '\n'.join(methods)
 constants = ''.join(
     f'static constexpr auto {key} = {value};\n'
     for key, value in re.findall(r'^\s*constexpr auto (\w+) = ([^;\n]+);', source, re.M)
     if re.search(r'\b' + key + r'\b', extracted)
 )
 (out/f'{label}_events_under_test.h').write_text(constants + extracted)
