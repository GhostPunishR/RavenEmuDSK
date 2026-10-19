#!/usr/bin/env python3
"""Guard the single-profile RetroAchievements navigation and connected avatar."""
from pathlib import Path

shell = Path("src/ui/app_shell.cpp").read_text(encoding="utf-8")
widgets = Path("src/ui/widgets.cpp").read_text(encoding="utf-8")
head = Path("include/ravenemudsk/ui/widgets.hpp").read_text(encoding="utf-8")
assert 'Page::Achievements' not in shell, "Separate Achievements page is still reachable"
assert '"Succès"},' not in shell, "Sidebar Succès entry must be absent"
assert 'draw_ra_identity(s,d,{pos.x+43.f,pos.y+44.f},28.f)' in shell, "Home avatar must depend on RA login"
assert 's.ra_images->image(snap.avatar_url)' in shell, "RA avatar must load from the SDK profile"
assert 'void draw_ra_mark(' in widgets and 'void draw_ra_mark(' in head, "RA emblem must be vector-drawn"
assert 'IM_COL32(20,101,220,255)' in widgets, "Blue R not present"
assert 'IM_COL32(216,165,0,255)' in widgets, "Gold A not present"
print("RA navigation and artwork checks OK")
