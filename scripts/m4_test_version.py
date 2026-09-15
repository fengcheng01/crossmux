"""Increment local M4 test revisions when build inputs change (not on rebuild)."""

import hashlib
import json
import re
from pathlib import Path


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".py", ".yaml", ".yml", ".json", ".ini"}
VERSION_PATTERN = re.compile(r"(CROSSPOINT_VERSION=[^\n]*?\.v)(\d+)")


def source_digest(project):
    files = []
    for directory in ("src", "lib", "freeink-sdk/libs", "scripts"):
        for path in (project / directory).rglob("*"):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            if ".generated." in path.name or any(part.startswith(".") for part in path.relative_to(project).parts):
                continue
            # Generated resources change during a build; hash their source inputs instead.
            if path.name.startswith("I18n") or "builtinFonts" in path.parts or "generated" in path.parts:
                continue
            files.append(path)
    files.extend(path for path in (project / "platformio.ini", project / "platformio.local.ini") if path.exists())
    files.extend(project.glob("*.csv"))
    digest = hashlib.sha256()
    for path in sorted(files):
        digest.update(path.relative_to(project).as_posix().encode())
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def select_revision(state, fingerprint, seed):
    if state is None:
        return seed + 1
    previous = max(seed, int(state["revision"]))
    if state["fingerprint"] != fingerprint or int(state["revision"]) < seed:
        return previous + 1
    return previous


def inject_version(env):
    if env["PIOENV"] != "murphy_m4_cn" or env.IsCleanTarget():
        return
    flags = env.get("BUILD_FLAGS", [])
    flags = flags if isinstance(flags, str) else "\n".join(flags)
    match = VERSION_PATTERN.search(flags)
    if match is None:
        return  # Release versions without the local .vN marker stay unchanged.
    project = Path(env["PROJECT_DIR"])
    state_path = project / ".pio" / "m4-test-version.json"
    # A malformed state is an error: silently resetting it could reuse a revision.
    state = json.loads(state_path.read_text()) if state_path.exists() else None
    fingerprint = source_digest(project)
    revision = select_revision(state, fingerprint, int(match[2]))
    flags = VERSION_PATTERN.sub(lambda m: m[1] + str(revision), flags)
    env.Replace(BUILD_FLAGS=flags.splitlines())
    state_path.parent.mkdir(parents=True, exist_ok=True)
    new_state = {"revision": revision, "fingerprint": fingerprint}
    if state != new_state:
        temporary = state_path.with_suffix(".tmp")
        temporary.write_text(json.dumps(new_state, indent=2) + "\n")
        temporary.replace(state_path)
    print(f"M4 test firmware revision: v{revision} (source fingerprint {fingerprint[:12]})")


try:
    Import("env")  # noqa: F821 -- PlatformIO/SCons entry point
except NameError:
    pass  # Importable by host tests without invoking PlatformIO.
else:
    inject_version(env)  # noqa: F821
