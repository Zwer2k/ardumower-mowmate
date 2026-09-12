"""
PlatformIO pre-build script: (re)generate src/sys/asset_bundle.h so that the
embedded web UI matches the target of the environment being built.

The UI is built per target: with ENABLE_MAP / ENABLE_LIVE_MAP /
ENABLE_GPS_DASHBOARD the bundle also contains the map, live-map and GPS
dashboard pages (~100 KB more). The plain ESP32 image (min_spiffs, 1.9 MB
app partition) does not fit with that variant embedded.

`task compile-pio` and the CI pipeline already build the right bundle before
calling pio; this script makes a bare `pio run` (and the IDE build button)
behave the same way. The bundle is only rebuilt when the UI flags derived from
the environment's build_flags or the UI sources changed.

Requires `npm` (with ui/node_modules installed) and `go` on PATH. If they are
missing, an existing bundle is kept and a warning is printed.
"""

import hashlib
import os
import shutil
import subprocess
import sys

Import("env")  # noqa: F821 (PlatformIO injects this)

PROJECT_DIR = env.subst("$PROJECT_DIR")
UI_DIR = os.path.join(PROJECT_DIR, "ui")
PACKAGER_DIR = os.path.join(PROJECT_DIR, "util", "package_ui")
BUNDLE_FILE = os.path.join(PROJECT_DIR, "src", "sys", "asset_bundle.h")
STAMP_FILE = os.path.join(PROJECT_DIR, ".pio", "asset_bundle.stamp")

# Firmware define -> Vite flag. Keep in sync with Taskfile.yml / ci.yml.
FLAG_MAP = {
    "ENABLE_MAP": "VITE_ENABLE_MAP",
    "ENABLE_LIVE_MAP": "VITE_ENABLE_LIVE_MAP",
    "ENABLE_GPS_DASHBOARD": "VITE_ENABLE_GPS_DASHBOARD",
}


def _log(msg):
    print("[ui_bundle] " + msg)


def _firmware_defines():
    """Collect defines from CPPDEFINES and raw build_flags (-D X / -DX)."""
    defines = set()
    for item in env.get("CPPDEFINES", []):
        name = item[0] if isinstance(item, (list, tuple)) else item
        defines.add(str(name))
    flags = env.get("BUILD_FLAGS", [])
    if isinstance(flags, str):
        flags = flags.split()
    tokens = []
    for f in flags:
        tokens.extend(str(f).split())
    for i, tok in enumerate(tokens):
        if tok == "-D" and i + 1 < len(tokens):
            defines.add(tokens[i + 1].split("=")[0])
        elif tok.startswith("-D") and len(tok) > 2:
            defines.add(tok[2:].split("=")[0])
    return defines


def _vite_env():
    defines = _firmware_defines()
    return {vite: ("true" if fw in defines else "false") for fw, vite in FLAG_MAP.items()}


def _source_signature():
    """Newest mtime of everything that influences the bundle."""
    roots = [
        os.path.join(UI_DIR, "src"),
        os.path.join(UI_DIR, "static"),
        PACKAGER_DIR,
    ]
    files = [
        os.path.join(UI_DIR, "package.json"),
        os.path.join(UI_DIR, "package-lock.json"),
        os.path.join(UI_DIR, "vite.config.js"),
        os.path.join(UI_DIR, "svelte.config.js"),
    ]
    newest = 0.0
    for root in roots:
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in ("node_modules", "build", ".svelte-kit")]
            for name in filenames:
                try:
                    newest = max(newest, os.path.getmtime(os.path.join(dirpath, name)))
                except OSError:
                    pass
    for f in files:
        try:
            newest = max(newest, os.path.getmtime(f))
        except OSError:
            pass
    return "%.0f" % newest


def _stamp(vite_env):
    payload = "|".join("%s=%s" % kv for kv in sorted(vite_env.items())) + "|" + _source_signature()
    return hashlib.sha1(payload.encode("utf-8")).hexdigest()


def _read_stamp():
    try:
        with open(STAMP_FILE, "r") as fh:
            return fh.read().strip()
    except OSError:
        return ""


def _write_stamp(value):
    os.makedirs(os.path.dirname(STAMP_FILE), exist_ok=True)
    with open(STAMP_FILE, "w") as fh:
        fh.write(value)


def _run(cmd, cwd, extra_env=None):
    run_env = dict(os.environ)
    if extra_env:
        run_env.update(extra_env)
    _log("running: %s (in %s)" % (" ".join(cmd), os.path.relpath(cwd, PROJECT_DIR)))
    result = subprocess.run(cmd, cwd=cwd, env=run_env)
    return result.returncode == 0


def main():
    vite_env = _vite_env()
    _log("env %s -> %s" % (env["PIOENV"], ", ".join("%s=%s" % kv for kv in sorted(vite_env.items()))))

    stamp = _stamp(vite_env)
    if os.path.isfile(BUNDLE_FILE) and _read_stamp() == stamp:
        _log("asset_bundle.h is up to date")
        return

    npm = shutil.which("npm")
    go = shutil.which("go")
    node_modules = os.path.isdir(os.path.join(UI_DIR, "node_modules"))
    if not (npm and go and node_modules):
        missing = [n for n, ok in (("npm", npm), ("go", go), ("ui/node_modules (run `npm install` in ui/)", node_modules)) if not ok]
        if os.path.isfile(BUNDLE_FILE):
            _log("WARNING: cannot rebuild the UI bundle (missing: %s); keeping the existing "
                 "asset_bundle.h, which may belong to a different target!" % ", ".join(missing))
            return
        sys.stderr.write("[ui_bundle] ERROR: src/sys/asset_bundle.h is missing and cannot be generated "
                         "(missing: %s)\n" % ", ".join(missing))
        env.Exit(1)

    if not _run([npm, "run", "build"], UI_DIR, vite_env):
        sys.stderr.write("[ui_bundle] ERROR: UI build failed\n")
        env.Exit(1)
    if not _run([go, "run", "."], PACKAGER_DIR):
        sys.stderr.write("[ui_bundle] ERROR: packaging the UI bundle failed\n")
        env.Exit(1)

    _write_stamp(stamp)
    _log("asset_bundle.h regenerated")


main()
