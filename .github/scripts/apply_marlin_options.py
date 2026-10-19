#!/usr/bin/env python3
"""Toggle Marlin feature defines in the checked-out configuration (used by the build workflow).

Usage: apply_marlin_options.py NAME=true|false [NAME=true|false ...]

The macro is searched in Configuration.h and Configuration_adv.h. Disabling a macro that does
not exist is a no-op; enabling one that does not exist is an error (for example OCTOPRINT_PLUGIN
only exists on the c13_octoprint branch).
"""
import re
import sys
from pathlib import Path

FILES = [Path("Marlin/Configuration.h"), Path("Marlin/Configuration_adv.h")]

# A feature can only be enabled if the feature it depends on is enabled too.
DEPENDS = {
    "DWIN_PLR_MENU": "POWER_LOSS_RECOVERY",
    "DWIN_SKEW_MENU": "SKEW_CORRECTION",
    "DWIN_INPUT_SHAPING_MENU": "FT_MOTION",
}


def to_bool(v: str) -> bool:
    return v.strip().lower() in ("1", "true", "yes", "on")


def set_define(macro: str, enabled: bool) -> bool:
    pattern = re.compile(r"^(\s*)(//\s*)?#define\s+" + re.escape(macro) + r"(\b.*)$", re.MULTILINE)
    for path in FILES:
        text = path.read_text(encoding="utf-8")
        if not pattern.search(text):
            continue
        prefix = "" if enabled else "//"
        text = pattern.sub(lambda m: f"{m.group(1)}{prefix}#define {macro}{m.group(3)}", text, count=1)
        path.write_text(text, encoding="utf-8")
        return True
    return False


def main() -> int:
    wanted = {}
    for arg in sys.argv[1:]:
        name, _, value = arg.partition("=")
        wanted[name] = to_bool(value)

    for macro, dep in DEPENDS.items():
        if wanted.get(macro) and not wanted.get(dep, False):
            print(f"{macro} requires {dep}; forcing {macro}=false")
            wanted[macro] = False

    status = 0
    for macro, enabled in wanted.items():
        found = set_define(macro, enabled)
        if not found and enabled:
            print(f"ERROR: {macro} not found in the configuration of this branch", file=sys.stderr)
            status = 1
        else:
            print(f"{macro} -> {'enabled' if enabled else 'disabled'}{'' if found else ' (not present, skipped)'}")
    return status


if __name__ == "__main__":
    sys.exit(main())
