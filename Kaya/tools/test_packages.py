#!/usr/bin/env python3
"""Compile every C++ package and run its smoke command."""
from pathlib import Path
import json
import subprocess
import tempfile
import tarfile
import os

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = ROOT / "Packages"

def run(command: list[str], env=None) -> None:
    subprocess.run(command, check=True, cwd=ROOT, env=env)

def main() -> None:
    packages = []
    with tempfile.TemporaryDirectory(prefix="kaya-package-check-") as temp:
        out = Path(temp)
        for folder in sorted(PACKAGES.iterdir()):
            manifest_path = folder / "manifest.json"
            if not folder.is_dir() or not manifest_path.is_file():
                continue
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            if manifest.get("runtime") != "cpp":
                continue
            entry = manifest["entry"]
            binary = out / manifest["name"]
            run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                 "-O0", str(folder / entry), "-o", str(binary)])
            run([str(binary), "--help"])
            archive = folder / f"{manifest['name']}.kay"
            if not archive.is_file():
                raise SystemExit(f"{manifest['name']}: .kay archive missing")
            with tarfile.open(archive) as tar:
                names = set(tar.getnames())
                if "manifest.json" not in names or entry not in names:
                    raise SystemExit(f"{manifest['name']}: incomplete .kay archive")
                if not set(manifest.get("supportFiles", [])).issubset(names):
                    raise SystemExit(f"{manifest['name']}: support file missing from .kay archive")
                archived_manifest = json.load(tar.extractfile("manifest.json"))
                if archived_manifest["name"] != manifest["name"]:
                    raise SystemExit(f"{manifest['name']}: archive manifest mismatch")
            packages.append(manifest["name"])
        for name in packages:
            if name in {"small-creatures", "agent-switcher", "nexus"}:
                continue
            result = subprocess.run([str(out / name)], input=b"5 7 11 * *",
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if result.returncode != 0 or b"Unknown tool" in result.stderr:
                raise SystemExit(f"{name}: catalog command failed to launch: {result.stderr!r}")
        # Exercise representative catalog behavior beyond --help.
        def output(package: str, *args: str, stdin: str = "") -> str:
            result = subprocess.run([str(out / package), *args], input=stdin,
                                    text=True, check=True, stdout=subprocess.PIPE)
            return result.stdout.strip()
        checks = {
            ("text-upper", ("hello",)): "HELLO",
            ("base64-encode", ("hello",)): "aGVsbG8=",
            ("base64-decode", ("aGVsbG8=",)): "hello",
            ("hex-encode", ("Hi",)): "4869",
            ("hex-decode", ("4869",)): "Hi",
            ("url-encode", ("a b",)): "a%20b",
            ("url-decode", ("a%20b",)): "a b",
            ("rot13", ("hello",)): "uryyb",
            ("num-factorial", ("5",)): "120",
            ("num-fibonacci", ("10",)): "55",
            ("num-prime", ("17",)): "true",
            ("num-gcd", ("12", "18")): "6",
            ("num-convert-hex", ("255",)): "ff",
            ("num-reverse", ("1203",)): "3021",
            ("num-prime-factors", ("12",)): "2 2 3",
            ("num-divisors", ("12",)): "1 2 3 4 6 12",
            ("num-range", ("1", "5", "2")): "1, 3, 5",
            ("num-mode", ("2", "3", "3", "4")): "3",
            ("num-modpow", ("2", "10", "1000")): "24",
            ("morse-encode", ("SOS",)): "... --- ...",
            ("html-escape", ("<a>",)): "&lt;a&gt;",
            ("json-escape", ("a\nb",)): '"a\\nb"',
            ("unicode-codepoints", ("Aé",)): "U+41 U+E9",
            ("csv-to-tsv", ("a,b",)): "a\tb",
            ("gen-rgb", ()): "rgb(79, 134, 247)",
            ("gen-box", ("Hi",)): "+----+\n| Hi |\n+----+",
            ("dev-path-extension", ("folder/file.txt",)): "txt",
            ("data-list-dedupe", ("a\na\nb",)): "a\nb",
            ("data-json-keys", ('{"name":"Kaya","enabled":true}',)): "name\nenabled",
            ("data-csv-stats", ("a,b\n1,2",)): "rows=2\ncolumns=2",
            ("data-file-size", ("hello",)): "5 bytes",
            ("data-list-rotate", ("a\nb\nc",)): "b\nc\na",
            ("dev-semver", ("2.4.1",)): "valid 2.4.1",
            ("dev-semver-bump", ("2.4.1", "minor")): "2.5.0",
            ("dev-shell-quote", ("hello world",)): "'hello world'",
            ("dev-sha256", ("abc",)): "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            ("dev-crc32", ("123456789",)): "cbf43926",
            ("dev-regex-unescape", (r"a\+b",)): "a+b",
            ("dev-xml-escape", ("<x>",)): "&lt;x&gt;",
            ("dev-ini-format", ("a=1\nb =2",)): "a = 1\nb = 2",
            ("data-log-levels", ("INFO started\nERROR failed\nWARN slow",)): "ERROR=1 WARN=1 INFO=1",
            ("data-accesslog-summary", ('GET / " 200 1\nGET /x " 404 0',)): "200 1\n404 1",
        }
        for (package, args), expected in checks.items():
            actual = output(package, *args)
            if actual != expected:
                raise SystemExit(f"{package}: expected {expected!r}, got {actual!r}")
        # Exercise the user-facing behavior of first-party packages, not just
        # compilation and --help output.
        sc = out / "small-creatures"
        sc_data = out / "sc-data"
        env = {**os.environ, "KAYA_DATA_DIR": str(sc_data)}
        run([str(sc), "--customize", "00FF00", "E"], env=env)
        run([str(sc), "--save", "1"], env=env)
        run([str(sc), "--customize", "E", "FF0000"], env=env)
        run([str(sc), "--load", "1"], env=env)
        if (sc_data / "current.theme").read_text().strip() != "00FF00 FFFFFF":
            raise SystemExit("small-creatures: save/load did not restore the theme")

        switcher = out / "agent-switcher"
        switcher_data = out / "switcher-data"
        run([str(switcher), "--use", "forge"], env={**os.environ, "KAYA_DATA_DIR": str(switcher_data)})
        fakebin = out / "fakebin"
        fakebin.mkdir()
        fake_forge = fakebin / "forge"
        fake_forge.write_text("#!/bin/sh\nprintf '%s\\n' \"$*\" > \"$FORGE_CAPTURE\"\n", encoding="utf-8")
        fake_forge.chmod(0o755)
        capture = out / "forge-args"
        fake_env = {**os.environ, "KAYA_DATA_DIR": str(switcher_data),
                    "FORGE_CAPTURE": str(capture), "PATH": str(fakebin) + os.pathsep + os.environ["PATH"]}
        subprocess.run([str(switcher), "--install", "dpkg"], input="y\n", text=True,
                       env=fake_env, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if capture.read_text().strip() != "install apt":
            raise SystemExit("agent-switcher: dpkg did not map to forge install apt")
        if (switcher_data / "selected-backend").read_text().strip() != "apt":
            raise SystemExit("agent-switcher: installed backend was not selected")

        nexus = out / "nexus"
        nexus_data = out / "nexus-data"
        nexus_env = {**os.environ, "KAYA_DATA_DIR": str(nexus_data)}
        run([str(nexus), "--init", "smoke"], env=nexus_env)
        result = subprocess.run([str(nexus), "--run", "smoke"], env=nexus_env,
                                check=True, text=True, stdout=subprocess.PIPE)
        if result.stdout.strip() != "42":
            raise SystemExit("nexus: VM smoke program did not produce 42")

        # End-to-end install and launch through Kaya with its real C++ plugin
        # and per-package sandbox/data directory.
        kaya_bin = out / "kaya"
        run(["cc", "-Wall", "-Wextra", "-O2", "-std=c11", "-o", str(kaya_bin),
             str(ROOT / "Kaya/src/kaya.c"), str(ROOT / "Kaya/src/sandbox.c"),
             str(ROOT / "Kaya/src/json.c"), str(ROOT / "Kaya/src/tar.c"),
             str(ROOT / "Kaya/src/manifest.c"), str(ROOT / "Kaya/src/setup.c")])
        kaya_root = out / "kaya-root"
        run([str(kaya_bin), "--root", str(kaya_root), "--install", "-fileplugin",
             str(ROOT / "Plugins/cpp/cpp.kyp")])
        for folder in sorted(PACKAGES.iterdir()):
            manifest_path = folder / "manifest.json"
            if manifest_path.is_file():
                archive = folder / f"{folder.name}.kay"
                run([str(kaya_bin), "--root", str(kaya_root), "--install", "-file", str(archive)])
        run([str(kaya_bin), "--root", str(kaya_root), "run", "small-creatures", "--", "--customize", "123456", "E"])
        if (kaya_root / "data/small-creatures/current.theme").read_text().strip() != "123456 FFFFFF":
            raise SystemExit("Kaya sandbox did not retain small-creatures data")
        run([str(kaya_bin), "--root", str(kaya_root), "run", "agent-switcher", "--", "--list"])
        run([str(kaya_bin), "--root", str(kaya_root), "run", "nexus", "--", "--init", "e2e"])
        vm_result = subprocess.run([str(kaya_bin), "--root", str(kaya_root), "run", "nexus", "--", "--run", "e2e"],
                                   check=True, text=True, stdout=subprocess.PIPE)
        if vm_result.stdout.strip() != "42":
            raise SystemExit("Nexus did not run through Kaya's sandbox")
        print(f"Compiled and smoke-checked {len(packages)} C++ packages")
        print("Packages: " + ", ".join(packages))

if __name__ == "__main__":
    main()
