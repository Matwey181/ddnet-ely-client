#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
import sys

root = Path(sys.argv[1])
p = root / "src/engine/client/client.cpp"
s = p.read_text()
old = "#elif defined(CONF_PLATFORM_IOS)\n\tconst char **argv = const_cast<const char **>(argv2);\n#elif defined(CONF_FAMILY_WINDOWS)"
assert s.count(old) == 1, "Unexpected iOS main entry point"
p.write_text(s.replace(old, "#elif defined(CONF_FAMILY_WINDOWS)", 1))
p = root / "src/game/client/components/bestclient/clientindicator/client_indicator.cpp"
s = p.read_text()
old = "const auto DesiredClientIds = BestClientIndicatorClient::CollectActiveLocalClientIds(GameClient()->m_aLocalIds, aClientActive);"
new = "int aConnectedLocalIds[NUM_DUMMIES];\n\tstd::copy_n(GameClient()->m_aLocalIds, NUM_DUMMIES, aConnectedLocalIds);\n\tconst auto DesiredClientIds = BestClientIndicatorClient::CollectActiveLocalClientIds(aConnectedLocalIds, aClientActive);"
assert s.count(old) == 1, "Unexpected client indicator registration"
p.write_text(s.replace(old, new, 1))
p = root / "ELY_SOURCE_LOCK.json"
lock = json.loads(p.read_text())
lock["post_patch_fixes_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
p.write_text(json.dumps(lock, indent=2) + "\n")
print("Applied iOS entry point and client indicator compatibility fixes")

# Native Files/Photos import uses UTTypeImage from this system framework.
p = root / "CMakeLists.txt"
s = p.read_text()
old = "  find_library(MEDIAPLAYER MediaPlayer)"
assert s.count(old) == 1
s = s.replace(old, old + "\n  find_library(UNIFORMTYPEIDENTIFIERS UniformTypeIdentifiers)", 1)
old = "    ${MEDIAPLAYER}\n"
assert s.count(old) == 1
s = s.replace(old, old + "    ${UNIFORMTYPEIDENTIFIERS}\n", 1)
p.write_text(s)
