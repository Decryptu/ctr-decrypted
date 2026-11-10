#!/usr/bin/env python3
"""Build and run the host harnesses under tools/test-*.c, tools/test-*.cpp and tools/test-*.py.

Each harness is a single translation unit compiled against the freestanding
_logic.h headers. Most carry their own build line in the leading comment block
(a line starting with cc, gcc or g++, possibly continued on following comment
lines). This runner uses that line when present and a default recipe otherwise.

Python harnesses (tools/test-*.py) are run directly with python3 from the repo root.
A line `cmd <name> :: <shell command>` in the list runs a non-file step (for example
a unittest discover) from the repo root. CI's ci.yml runs this script for all of
them, so the local run and CI use one list and cannot drift.

Which harnesses CI enforces is decided by tools/ci/harnesses.txt: one harness
path per line, optionally followed by `skip <reason>`. A harness file that is
neither listed nor skipped fails the run, so adding a harness means deciding
whether CI runs it.

Harnesses run in parallel, one per CPU (HARNESS_JOBS overrides it; 1 runs them
one at a time). Each builds to its own output file, and those that write files
do it in their own mkdtemp directory under /tmp (C:\tmp on Windows). Anything a
run leaves in /tmp is deleted when it ends. Results print in a fixed order.

Usage:
  tools/ci/run-harnesses.py            run the listed harnesses, fail on any failure
  tools/ci/run-harnesses.py --all      try every harness, report, never fail (discovery)
  tools/ci/run-harnesses.py --write-list  discovery, then rewrite harnesses.txt from the result
"""
import concurrent.futures, glob, os, re, shlex, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LIST = os.path.join(ROOT, "tools", "ci", "harnesses.txt")
OUT = os.environ.get("HARNESS_OUT", "/tmp/ctr-harness")
JSON_INC = os.environ.get("HARNESS_JSON_INCLUDE", "ap/vendor/json/include")
JOBS = int(os.environ.get("HARNESS_JOBS", "0")) or os.cpu_count() or 1

DEFAULT_C = "cc -Wall -Wextra -DCTR_NATIVE -DCTR_AP -I ap -I . -I include -o {out} {src} -lm"
# Harnesses without a header build line that compile a production unit name it
# in their text; map the name to the unit to link.
EXTRA_UNITS = {"ap_seedcfg": "ap/ap_seedcfg.cpp", "ap_charseat": "ap/ap_charseat.c"}

DEFAULT_CPP = ("g++ -m32 -std=c++17 -DCTR_AP -I ap -I . -I include -I " + JSON_INC +
               " -o {out} {src}")


def header_command(src):
    """Return the build command from the leading comment, or None."""
    lines = open(src, encoding="utf-8", errors="replace").read().splitlines()[:40]
    cmd = None
    for i, ln in enumerate(lines):
        if not ln.startswith("//"):
            if cmd is not None:
                break
            continue
        body = ln[2:].strip()
        if cmd is None:
            if re.match(r"^(cc|gcc|g\+\+|c\+\+|clang|clang\+\+)\s", body):
                cmd = body
            continue
        # continuation: option-looking line, or a line that names the source file
        if body.startswith("/tmp/") or body.startswith("&&"):
            break  # the run step; we run the binary ourselves
        if (body.startswith("-") or body.startswith("\\") or body.endswith("\\") or "-o /tmp/" in body
                or re.match(r"^(tools|ap|platform|game|include)/", body)):
            cmd += " " + body
        else:
            break
    if cmd is None:
        return None
    cmd = cmd.replace("\\", " ")
    # drop the trailing "&& /tmp/x" run step; we run the binary ourselves
    cmd = cmd.split("&&")[0].strip()
    return cmd


def build_command(src):
    name = os.path.splitext(os.path.basename(src))[0]
    # Forward slash, not os.path.join: on Windows a backslash would be eaten
    # by shlex.split below, and the binary would land beside the folder.
    out = OUT.rstrip("/") + "/" + name
    cmd = header_command(src)
    if cmd:
        # rewrite the header's -o target to our output dir
        cmd = re.sub(r"-o\s+\S+", lambda _: "-o " + out, cmd)
        if "-o " not in cmd:
            cmd += " -o " + out
        cmd = cmd.replace("ap/vendor/json/include", JSON_INC)
    else:
        tmpl = DEFAULT_CPP if src.endswith(".cpp") else DEFAULT_C
        cmd = tmpl.format(out=out, src=src)
        # production units a harness links against when it names them
        text = open(os.path.join(ROOT, src), encoding="utf-8", errors="replace").read()
        for needle, unit in EXTRA_UNITS.items():
            if needle in text:
                # g++ would compile a .c unit as C++, with mangled names
                cmd += f" -x c {unit} -x none" if src.endswith(".cpp") and unit.endswith(".c") else " " + unit
    return cmd, out


def run_py(src):
    cmd = "python3 " + src
    try:
        r = subprocess.run([sys.executable, src], cwd=ROOT, capture_output=True, text=True, timeout=600)
    except Exception as e:  # noqa: BLE001
        return "run-error", cmd, str(e)
    if r.returncode != 0:
        return "run-fail", cmd, (r.stdout + r.stderr)[-1500:]
    return "pass", cmd, ""


def run_cmd(cmd):
    try:
        r = subprocess.run(cmd, shell=True, cwd=ROOT, capture_output=True, text=True, timeout=1200)
    except Exception as e:  # noqa: BLE001
        return "run-error", cmd, str(e)
    if r.returncode != 0:
        return "run-fail", cmd, (r.stdout + r.stderr)[-1500:]
    return "pass", cmd, ""


def run_one(src):
    if src.endswith(".py"):
        return run_py(src)
    cmd, out = build_command(src)
    t0 = time.time()
    try:
        b = subprocess.run(shlex.split(cmd), cwd=ROOT, capture_output=True, text=True, timeout=600)
    except Exception as e:  # noqa: BLE001
        return "build-error", cmd, str(e)
    if b.returncode != 0:
        return "build-fail", cmd, (b.stderr or b.stdout)[-1500:]
    built = time.time() - t0
    try:
        r = subprocess.run([out], cwd=ROOT, capture_output=True, text=True, timeout=600)
    except Exception as e:  # noqa: BLE001
        return "run-error", cmd, str(e)
    if r.returncode != 0:
        return "run-fail", cmd, (r.stdout + r.stderr)[-1500:]
    return "pass", cmd, f"build {built:.1f}s run {time.time() - t0 - built:.1f}s"


def read_list():
    listed, skipped, cmds = {}, {}, {}
    if not os.path.exists(LIST):
        return listed, skipped, cmds
    for ln in open(LIST):
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        if ln.startswith("cmd "):
            name, _, command = ln[4:].partition("::")
            cmds[name.strip()] = command.strip()
            continue
        parts = ln.split(None, 2)
        if len(parts) >= 2 and parts[1] == "skip":
            skipped[parts[0]] = parts[2] if len(parts) > 2 else ""
        else:
            listed[parts[0]] = True
    return listed, skipped, cmds


def install_nosan_shim():
    """On Windows, put compilers that drop -fsanitize first on PATH (see nosan-shim.c)."""
    real = shutil.which("gcc")
    if os.name != "nt" or real is None:
        return
    # Windows looks in the launching program's own folder before PATH, so under
    # MinGW's python the real compilers next to it would win over the shims.
    if os.path.exists(os.path.join(os.path.dirname(sys.executable), "gcc.exe")):
        sys.exit("run this with a Python outside the MinGW bin folder, e.g. py -3 tools/ci/run-harnesses.py")
    shim_dir = os.path.join(OUT, "nosan")
    os.makedirs(shim_dir, exist_ok=True)
    first = os.path.join(shim_dir, "cc.exe")
    subprocess.run([real, "-O2", "-o", first, os.path.join(ROOT, "tools", "ci", "nosan-shim.c")], check=True)
    for name in ("gcc.exe", "g++.exe"):
        shutil.copyfile(first, os.path.join(shim_dir, name))
    os.environ["NOSAN_REAL_DIR"] = os.path.dirname(real)
    os.environ["PATH"] = shim_dir + os.pathsep + os.environ["PATH"]
    print("windows: harnesses build without -fsanitize (MinGW has no UBSan runtime)")


def main():
    tmp = os.path.abspath("/tmp")
    before = set(os.listdir(tmp)) if os.path.isdir(tmp) else set()
    try:
        return run(sys.argv[1] if len(sys.argv) > 1 else "ci")
    finally:
        # Harnesses don't all delete their mkdtemp folders; nothing in /tmp
        # outlives the run, the build output in OUT included.
        if os.path.isdir(tmp):
            for name in set(os.listdir(tmp)) - before:
                path = os.path.join(tmp, name)
                if os.path.isdir(path):
                    shutil.rmtree(path, ignore_errors=True)
                else:
                    try:
                        os.remove(path)
                    except OSError:
                        pass


def run(mode):
    os.makedirs(OUT, exist_ok=True)
    install_nosan_shim()
    all_src = sorted(glob.glob(os.path.join(ROOT, "tools", "test-*.c")) +
                     glob.glob(os.path.join(ROOT, "tools", "test-*.cpp")) +
                     glob.glob(os.path.join(ROOT, "tools", "test-*.py")))
    # harnesses.txt uses forward slashes; Windows Python's relpath does not.
    all_rel = [os.path.relpath(p, ROOT).replace(os.sep, "/") for p in all_src]
    listed, skipped, cmds = read_list()
    results = {}
    targets = all_rel if mode in ("--all", "--write-list") else [p for p in all_rel if p in listed]
    # The `cmd` steps go first: they are the longest (one builds SDL), so they
    # start while the short harnesses fill the other workers.
    jobs = [(name, run_cmd, command) for name, command in cmds.items()] if mode == "ci" else []
    jobs += [(rel, run_one, rel) for rel in targets]
    with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
        for (name, _, _), result in zip(jobs, pool.map(lambda job: job[1](job[2]), jobs)):
            status, cmd, detail = result
            results[name] = result
            print(f"{status:11s} {name}", flush=True)
            if status != "pass" and mode == "ci":
                print("  cmd: " + cmd)
                print("  " + detail.replace("\n", "\n  "))
    if mode == "--write-list":
        with open(LIST, "w") as f:
            f.write("# Harnesses CI runs (tools/ci/run-harnesses.py). One per line; `skip <reason>` opts out.\n")
            f.write("# Regenerate with: tools/ci/run-harnesses.py --write-list\n")
            for name, command in cmds.items():
                f.write(f"cmd {name} :: {command}\n")
            for rel in all_rel:
                st = results[rel][0]
                if st == "pass":
                    f.write(rel + "\n")
                else:
                    f.write(f"{rel} skip {st} under the generic recipe; give it a build line in its header comment\n")
        print("wrote " + LIST)
        return 0
    if mode == "--all":
        return 0
    failed = [r for r, v in results.items() if v[0] != "pass"]
    unlisted = [r for r in all_rel if r not in listed and r not in skipped]
    for r in unlisted:
        print(f"unlisted    {r}  (add it to tools/ci/harnesses.txt, or `skip <reason>`)")
    print(f"\n{len(results) - len(failed)} passed, {len(failed)} failed, {len(skipped)} skipped, {len(unlisted)} unlisted")
    return 1 if (failed or unlisted) else 0


if __name__ == "__main__":
    sys.exit(main())
