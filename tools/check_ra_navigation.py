#!/usr/bin/env python3
"""Guard the single-profile RetroAchievements navigation and bundled artwork."""
from pathlib import Path
import struct

# The launcher, one file per screen.
shell = "\n".join(p.read_text(encoding="utf-8") for p in sorted(Path("src/ui/shell").glob("*.[ch]pp")))
widgets = Path("src/ui/widgets.cpp").read_text(encoding="utf-8")
head = Path("include/ravenemudsk/ui/widgets.hpp").read_text(encoding="utf-8")
cmake = Path("CMakeLists.txt").read_text(encoding="utf-8")
logo = Path("assets/ra-logo.png").read_bytes()

assert 'Page::Achievements' not in shell, "Separate Achievements page is still reachable"
assert '"Succès"},' not in shell, "Sidebar Succès entry must be absent"
assert 'draw_ra_identity(s,d,{pos.x+43.f,pos.y+44.f},28.f)' in shell, "Home avatar must depend on RA login"
assert 's.ra_images->image(snap.avatar_url)' in shell, "RA avatar must load from the SDK profile"
assert 'state.ra_logo = load_brand_asset(renderer,"ra-logo.png");' in shell, "RA logo must load from packaged image"
assert 'if (state.ra_logo) SDL_DestroyTexture(state.ra_logo);' in shell, "RA logo texture must be released"
assert 'void draw_ra_mark(' not in widgets + head, "Legacy vector logo must be removed"
assert 'Icon::RetroAchievements' not in widgets + shell, "Legacy vector icon must be removed"
assert 'assets/ra-logo.png' in cmake, "CMake must bundle the PNG"
assert logo.startswith(bytes([137,80,78,71,13,10,26,10])), "Logo asset is not a PNG"
width, height = struct.unpack(">II", logo[16:24])
assert width >= 64 and height >= 32, "RA logo has invalid dimensions"
print("RA image, navigation, and avatar checks OK")
