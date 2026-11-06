"""Compile the production QoL service with only engine/IO boundaries faked."""
from pathlib import Path
import sys

source = (Path(__file__).parent / "../KH3APProbeCpp/kh3ap/features/story/quality_of_life_service.cpp").read_text()
start = source.index("    struct RuntimeQualityOfLifeService::Impl")
out = Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("namespace kh3ap {\nusing namespace RC;\n" + source[start:])
