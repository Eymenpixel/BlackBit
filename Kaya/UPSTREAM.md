# Imported Kaya snapshot

This directory is imported from `Eymenpixel/Kaya`, branch `kaya-v2-rework`, commit `5f22988` (2026-10-20). It contains the Kaya core, schemas, plugins, documentation, and 200 distributable package archives. The source branch is the open Kaya PR #1.

The POSIX manager is a WSL development backend. The freestanding Blackbit kernel currently reaches it through the restricted QEMU serial bridge in `tools/kaya_bridge.py`; this does not make POSIX package processes native kernel processes.
