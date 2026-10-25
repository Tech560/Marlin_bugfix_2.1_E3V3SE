#!/usr/bin/env python3
"""Fail when a feature that exists in the reference firmware is missing from a
new build, so that adding new features never silently drops an existing one.

Usage:
    python test/test_feature_parity.py REFERENCE.bin NEW.bin
"""
import re
import sys
from pathlib import Path

# marker string -> feature that provides it
MARKERS = {
    'One click print': 'ONE_CLICK_PRINT',
    'Power-loss recovery': 'DWIN_PLR_MENU',
    'M413 S': 'POWER_LOSS_RECOVERY',
    'Input Shaping': 'DWIN_INPUT_SHAPING_MENU',
    '4 points, D Routine': 'D_ROUTINE_AUTO_OFFSET',
    'Printer Statistics': 'DWIN_STATS_MENU',
    'M852 I': 'SKEW_CORRECTION',
    'Skew': 'DWIN_SKEW_MENU',
    'ABS SETTINGS': 'EXTRA_PREHEAT_LABELS',
    'Custom Extrude': 'DWIN_CUSTOM_EXTRUDE',
    'CUSTOM Z Height After Homing:': 'DWIN_ZHOME_MENU',
    'Dimm Brightness(%)': 'DWIN_DIMM_MENU',
    'Thumbnail': 'DWIN_RENDER_THUMBNAIL',
    'Fixed-Time': 'FT_MOTION',
}


def strings(path):
    """Printable ASCII runs of five characters or more."""
    data = Path(path).read_bytes()
    return {m.decode('ascii', 'ignore') for m in re.findall(rb'[\x20-\x7e]{5,}', data)}


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    ref, new = strings(argv[1]), strings(argv[2])
    tracked = {m: f for m, f in MARKERS.items() if any(m in s for s in ref)}
    missing = [(m, f) for m, f in tracked.items() if not any(m in s for s in new)]
    for marker, feature in missing:
        print(f'MISSING in new build: {marker!r} (feature {feature})')
    untracked = sorted(set(MARKERS) - set(tracked))
    if untracked:
        print(f'not present in the reference either, so not tracked: {", ".join(untracked)}')
    print(f'{len(tracked) - len(missing)}/{len(tracked)} tracked parity markers present')
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
