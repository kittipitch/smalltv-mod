#!/usr/bin/env python3
"""The pre-flash gate compares the image's git stamp with HEAD. A docs-only commit after the build must not refuse a
good image, and anything that could change the image must still refuse it. Builds throwaway repos; no hardware, no
PlatformIO. Run from anywhere."""
import shutil
import subprocess
import tempfile
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent / "preflash_check.sh"


def git(repo, *args):
    return subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@example.invalid", *args], cwd=repo,
                          check=True, capture_output=True, text=True).stdout.strip()


def commit(repo, path, text="x"):
    f = repo / path
    f.parent.mkdir(parents=True, exist_ok=True)
    with f.open("a") as out:   # append: one case edits the gate itself, which must still run
        out.write(f"\n# {text}\n")
    git(repo, "add", path)
    git(repo, "commit", "-qm", f"change {path}")
    return git(repo, "rev-parse", "--short", "HEAD")


def scenario(d, name, stamp_of, after=(), extra_token=None, stamp_override=None, push=True, move=None):
    """Build an image stamped at one commit, then add `after` commits. Returns (exit code, output)."""
    root = Path(d) / name
    bare, repo = root / "origin.git", root / "repo"
    root.mkdir()
    subprocess.run(["git", "init", "-q", "--bare", "-b", "main", str(bare)], check=True)
    subprocess.run(["git", "clone", "-q", str(bare), str(repo)], check=True, capture_output=True)
    git(repo, "checkout", "-qb", "main")
    (repo / ".gitignore").write_text(".pio/\n")
    git(repo, "add", ".gitignore")
    (repo / "scripts").mkdir()
    shutil.copy(SCRIPT, repo / "scripts/preflash_check.sh")
    git(repo, "add", "scripts/preflash_check.sh")
    git(repo, "commit", "-qm", "base")
    commit(repo, "src/main.cpp", "int a;")
    stamp = stamp_override or git(repo, "rev-parse", "--short", stamp_of)
    for path in after:
        commit(repo, path, f"edit {path}")
    if move:
        (repo / move[1]).parent.mkdir(parents=True, exist_ok=True)
        git(repo, "mv", move[0], move[1])
        git(repo, "commit", "-qm", "move")
    if push:
        git(repo, "push", "-q", "origin", "main")
    image = repo / ".pio/build/e/firmware.bin"
    image.parent.mkdir(parents=True)
    tokens = [stamp] + ([extra_token] if extra_token else [])
    image.write_bytes(b"\xe9\0\0\0" + b"".join(b"\0\0" + t.encode() + b"\0\0" for t in tokens) + b"\0" * 64)
    r = subprocess.run(["bash", "scripts/preflash_check.sh", "e"], cwd=repo, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


with tempfile.TemporaryDirectory() as d:
    rc, out = scenario(d, "same", "HEAD")
    assert rc == 0 and "CLEARED" in out, out

    rc, out = scenario(d, "docs_after", "HEAD", ["README.md", "docs/page.md", "CHANGELOG.md"])
    assert rc == 0 and "CLEARED" in out, "docs-only commits after the build must not refuse the image\n" + out

    rc, out = scenario(d, "tests_after", "HEAD", ["scripts/test_x.py", "scripts/preflash_check.sh"])
    assert rc == 0, "the gate and tests are not in the image\n" + out

    rc, out = scenario(d, "src_after", "HEAD", ["src/main.cpp"])
    assert rc == 1 and "REFUSED" in out and "src/main.cpp" in out, out

    rc, out = scenario(d, "mixed_after", "HEAD", ["README.md", "platformio.ini"])
    assert rc == 1 and "platformio.ini" in out, "one image-affecting file among docs must refuse\n" + out

    rc, out = scenario(d, "build_script_after", "HEAD", ["scripts/git_sha.py"])
    assert rc == 1, "only the gate and tests are exempt under scripts/\n" + out

    rc, out = scenario(d, "renamed_out", "HEAD", move=("src/main.cpp", "docs/main.md"))
    assert rc == 1 and "src/main.cpp" in out, "moving a source file into docs/ removes it from the image\n" + out

    rc, out = scenario(d, "no_such_commit", "HEAD", stamp_override="abcdef1")
    assert rc == 1 and "REFUSED" in out, "a stamp that is no commit must refuse\n" + out

    rc, out = scenario(d, "dirty", "HEAD", extra_token="abcdef1+")
    assert rc == 1 and "DIRTY" in out, out

    rc, out = scenario(d, "other_hex", "HEAD", extra_token="deadbee")
    assert rc == 0, "a hex-looking string that is no commit must not be read as the stamp\n" + out

    rc, out = scenario(d, "unpushed", "HEAD", ["README.md"], push=False)
    assert rc == 1 and "NOT on origin" in out, out

print("test_preflash_check: OK")
