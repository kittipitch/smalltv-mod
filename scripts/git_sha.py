# Stamps the build with the git SHA it was produced from.
#
# FW_VERSION only changes when a release tag is cut, so many different builds
# report the same string -- on 2026-09-11 that sent a whole audit at code the
# hardware was not running (a unit reported 1.0.0-kitt23 while serving a field
# that does not exist in that tag). The SHA makes a unit self-identifying.
Import("env")
import subprocess

def _sha():
    try:
        s = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"],
                                    stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return "unknown"
    if not s:
        return "unknown"
    # A dirty tree is NOT the commit it claims to be. Mark it, or the SHA on a
    # hand-flashed test image lies in exactly the situation it exists to fix.
    try:
        # --porcelain, not `diff --quiet`: PlatformIO compiles newly added files
        # under build_src_filter, and an UNTRACKED .cpp is exactly the case where
        # the SHA would otherwise claim a clean commit it does not match.
        st = subprocess.check_output(["git", "status", "--porcelain"],
                                     stderr=subprocess.DEVNULL).decode().strip()
        if st:
            s += "+"
    except Exception:
        pass
    return s

env.Append(CPPDEFINES=[("GIT_SHA", env.StringifyMacro(_sha()))])
