#!/usr/bin/env python3
"""Validate and bundle the distributable Kaya package/plugin archives."""
from __future__ import annotations

import argparse
import hashlib
import json
import tarfile
import zipfile
from pathlib import Path


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def validate_package(folder: Path) -> tuple[Path, dict[str, str]]:
    manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    name = manifest["name"]
    if name != folder.name:
        raise ValueError(f"{folder}: manifest name does not match folder")
    archive = folder / f"{name}.kay"
    expected = {"manifest.json", manifest["entry"], *manifest.get("supportFiles", [])}
    with tarfile.open(archive, "r:*") as tar:
        members = {m.name for m in tar.getmembers() if m.isfile()}
        if not expected.issubset(members):
            raise ValueError(f"{archive}: missing files {sorted(expected - members)}")
        archived = json.load(tar.extractfile("manifest.json"))
        if archived != manifest:
            raise ValueError(f"{archive}: archived manifest differs from source")
    return archive, {"name": name, "file": f"Kaya/Packages/{name}/{archive.name}", "sha256": digest(archive)}


def validate_plugin(folder: Path) -> tuple[Path, dict[str, str]]:
    archive = folder / f"{folder.name}.kyp"
    with tarfile.open(archive, "r:*") as tar:
        members = {m.name for m in tar.getmembers() if m.isfile()}
        if not {"plugin.json", "run"}.issubset(members):
            raise ValueError(f"{archive}: plugin.json or run entry missing")
        metadata = json.load(tar.extractfile("plugin.json"))
        if metadata.get("name") != folder.name:
            raise ValueError(f"{archive}: plugin name does not match folder")
    return archive, {"name": folder.name, "file": f"Kaya/Plugins/{folder.name}/{archive.name}", "sha256": digest(archive)}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.repo.resolve()
    package_dirs = sorted(p for p in (root / "Kaya/Packages").iterdir() if (p / "manifest.json").is_file())
    plugin_dirs = sorted(p for p in (root / "Kaya/Plugins").iterdir() if p.is_dir())
    packages = [validate_package(p) for p in package_dirs]
    plugins = [validate_plugin(p) for p in plugin_dirs]
    if len(packages) != 200:
        raise SystemExit(f"expected 200 Kaya packages, found {len(packages)}")
    if len(plugins) != 4:
        raise SystemExit(f"expected 4 Kaya plugins, found {len(plugins)}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    manifest = {"packageCount": len(packages), "pluginCount": len(plugins),
                "packages": [item[1] for item in packages],
                "plugins": [item[1] for item in plugins]}
    with zipfile.ZipFile(args.output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
        for path, _ in packages + plugins:
            bundle.write(path, path.relative_to(root).as_posix())
        for relative in ("Kaya/Packages/README.md", "Kaya/Plugins/README.md", "Kaya/UPSTREAM.md"):
            bundle.write(root / relative, relative)
        bundle.writestr("release-manifest.json", json.dumps(manifest, indent=2) + "\n")
    print(f"Validated and bundled {len(packages)} packages and {len(plugins)} plugins: {args.output}")


if __name__ == "__main__":
    main()
