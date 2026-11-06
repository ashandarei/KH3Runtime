"""Exercise production space notification/publication methods with engine and IO faked."""
from pathlib import Path
import sys

source = (Path(__file__).parent / "../KH3APProbeCpp/kh3ap/features/world/space/space_service.cpp").read_text()
def method(name):
    start = source.index("    void RuntimeSpaceService::" + name + "(")
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

out = Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("namespace kh3ap {\n" + "\n".join(method(name) for name in (
    "Impl::publish_status", "notify_context_changed", "notify_map_ready")) + "\n}\n")
