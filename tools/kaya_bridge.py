#!/usr/bin/env python3
"""Run the WSL Kaya development manager behind Blackbit's QEMU COM1 bridge."""
from __future__ import annotations
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
KERNEL_DIR = BASE / "kernel"
KAYA_SOURCE = Path(os.environ.get("BLACKBIT_KAYA_SOURCE", BASE.parent / "Kaya")).expanduser()
KAYA = KAYA_SOURCE / "Kaya" / "build" / "kaya"
ROOT = Path(os.environ.get("BLACKBIT_KAYA_ROOT", Path.home() / ".local/share/blackbit-kaya"))
PORT = 45891
NAME_RE = re.compile(r"^[A-Za-z0-9._-]{1,64}$")
ARG_RE = re.compile(r"^[A-Za-z0-9._:/=%+-]{1,128}$")


def run_kaya(request: str) -> tuple[int, bytes]:
    parts = request.strip().split()
    if not parts or parts[0] != "KAYA":
        return 2, b"invalid request\n"
    args = parts[1:]
    if len(args) == 1 and args[0] in {"list", "relook", "upgrade"}:
        command = [str(KAYA), "list" if args[0] == "list" else "--relook" if args[0] == "relook" else "--upgrade"]
    elif len(args) == 2 and args[0] in {"info", "install", "remove"}:
        if not NAME_RE.fullmatch(args[1]):
            return 2, b"invalid package name\n"
        action = {"info": ["info", args[1]], "install": ["--install", args[1]], "remove": ["--remove", args[1]]}[args[0]]
        command = [str(KAYA), *action]
    elif len(args) >= 2 and args[0] == "run":
        if not NAME_RE.fullmatch(args[1]) or len(args) > 9:
            return 2, b"invalid Kaya run request\n"
        app_args = args[2:]
        if any(not ARG_RE.fullmatch(arg) for arg in app_args):
            return 2, b"invalid package argument\n"
        command = [str(KAYA), "run", args[1], "--", *app_args]
    else:
        return 2, b"usage: KAYA list | info/install/remove <name> | relook | upgrade | run <name> [args...]\n"

    ROOT.mkdir(parents=True, exist_ok=True)
    try:
        result = subprocess.run(
            [*command, "--root", str(ROOT)], cwd=ROOT, stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180, check=False,
        )
        output = result.stdout.decode("utf-8", errors="replace")
        payload = output.encode("ascii", errors="replace")
        if len(payload) > 8192:
            payload = payload[:8180] + b"\n[output truncated]\n"
        return result.returncode, payload
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or b""
        if isinstance(output, str):
            output = output.encode("utf-8", errors="replace")
        return 124, output[:8000] + b"\nKaya request timed out\n"
    except OSError as exc:
        return 127, f"could not start Kaya: {exc}\n".encode("ascii", errors="replace")


def run_host_apt(request: str) -> tuple[int, bytes]:
    parts = request.strip().split()
    if not parts or parts[0] != "HOST" or len(parts) > 9 or parts[1:2] != ["apt"]:
        return 2, b"usage: apt [arguments...]\n"
    args = parts[2:]
    if any(not ARG_RE.fullmatch(arg) for arg in args):
        return 2, b"invalid APT argument\n"
    apt = shutil.which("apt")
    if not apt:
        return 127, b"Ubuntu APT was not found on the WSL host\n"
    try:
        result = subprocess.run([apt, *args], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180, check=False)
        payload = result.stdout.decode("utf-8", errors="replace").encode("ascii", errors="replace")
        return result.returncode, payload[:8192]
    except (subprocess.TimeoutExpired, OSError) as exc:
        return 124, f"APT request failed: {exc}\n".encode("ascii", errors="replace")


def send_response(conn: socket.socket, status: int, payload: bytes) -> None:
    conn.sendall(f"KAYA-RESP {len(payload)} {1 if status else 0}\n".encode("ascii"))
    for offset in range(0, len(payload), 4):
        conn.sendall(payload[offset:offset + 4])
        time.sleep(0.001)


def serve(conn: socket.socket) -> None:
    with conn, conn.makefile("rb") as reader:
        while line := reader.readline(256):
            try:
                request = line.decode("ascii", errors="strict")
            except UnicodeDecodeError:
                continue
            if request.startswith("KAYA "):
                status, payload = run_kaya(request)
            elif request.startswith("HOST apt"):
                status, payload = run_host_apt(request)
            else:
                continue
            send_response(conn, status, payload)


def main() -> int:
    if not KAYA.is_file():
        build = subprocess.run(["make", "-C", str(KAYA_SOURCE / "Kaya")], check=False)
        if build.returncode or not KAYA.is_file():
            print("Kaya build failed; set BLACKBIT_KAYA_SOURCE to your Kaya repository and run make -C Kaya", file=sys.stderr)
            return 1
    qemu = shutil.which("qemu-system-i386")
    if not qemu:
        print("qemu-system-i386 is required", file=sys.stderr)
        return 127
    image = KERNEL_DIR / "build" / "blackbit-bios.img"
    if not image.is_file():
        print("BIOS image missing; run make -C kernel boot-image", file=sys.stderr)
        return 1
    command = [qemu, "-name", "Blackbit OS", "-drive", f"file={image},format=raw,if=ide,index=0",
               "-boot", "c", "-m", "128M", "-no-reboot", "-serial",
               f"tcp:127.0.0.1:{PORT},server=on,wait=off"]
    child = subprocess.Popen(command)
    def stop_child(_signum: int, _frame: object) -> None:
        if child.poll() is None:
            child.terminate()
    signal.signal(signal.SIGINT, stop_child)
    signal.signal(signal.SIGTERM, stop_child)
    deadline = time.monotonic() + 20
    conn = None
    while time.monotonic() < deadline and child.poll() is None:
        try:
            conn = socket.create_connection(("127.0.0.1", PORT), timeout=0.5)
            break
        except OSError:
            time.sleep(0.1)
    if conn is None:
        stop_child(signal.SIGTERM, None)
        print("Could not connect the Kaya bridge to QEMU COM1", file=sys.stderr)
        return 1
    try:
        serve(conn)
    finally:
        stop_child(signal.SIGTERM, None)
    return child.wait()


if __name__ == "__main__":
    raise SystemExit(main())
