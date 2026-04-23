"""PlatformIO extra_script: copia firmware.bin para firmware_versions/FitaDigital_v<VER>.bin apos build."""
import os
import re
import shutil

Import("env")  # noqa: F821

def _extract_version():
    for flag in env.get("BUILD_FLAGS", []):  # noqa: F821
        m = re.search(r'FITADIGITAL_VERSION="([^"]+)"', flag)
        if m:
            return m.group(1)
    return "unknown"

def _archive_bin(source, target, env):  # noqa: ARG001
    ver = _extract_version()
    src = str(target[0])
    if not os.path.isfile(src):
        return
    proj_dir = env["PROJECT_DIR"]
    out_dir = os.path.join(proj_dir, "firmware_versions")
    os.makedirs(out_dir, exist_ok=True)
    dst = os.path.join(out_dir, "FitaDigital_v{}.bin".format(ver))
    shutil.copy2(src, dst)
    print("[save_firmware_version] copied -> {}".format(dst))

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _archive_bin)  # noqa: F821
