from pathlib import Path
import sys

source = (Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/player/state/shared_sora_progression.cpp').read_text()
source = source[source.index('namespace kh3ap'):]
Path(sys.argv[1]).write_text(source)
