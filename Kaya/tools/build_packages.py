#!/usr/bin/env python3
"""Build deterministic .kay tar archives from package source directories."""
from pathlib import Path
import json
import tarfile

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = ROOT / "Packages"

def main() -> None:
    built = 0
    for folder in sorted(PACKAGES.iterdir()):
        manifest = folder / "manifest.json"
        if not folder.is_dir() or not manifest.is_file():
            continue
        data = json.loads(manifest.read_text(encoding="utf-8"))
        entry = data.get("entry")
        if not entry or not (folder / entry).is_file():
            raise SystemExit(f"{folder.name}: manifest entry is missing")
        archive = folder / f"{folder.name}.kay"
        support = data.get("supportFiles", [])
        if not isinstance(support, list) or any(not isinstance(x, str) for x in support):
            raise SystemExit(f"{folder.name}: supportFiles must be a list of paths")
        names = list(dict.fromkeys(["manifest.json", entry, *support]))
        for name in names:
            path = (folder / name).resolve()
            if folder.resolve() not in path.parents or not path.is_file():
                raise SystemExit(f"{folder.name}: invalid or missing package file: {name}")
        with tarfile.open(archive, "w", format=tarfile.USTAR_FORMAT) as tar:
            for name in names:
                path = folder / name
                info = tar.gettarinfo(str(path), arcname=name)
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                info.mtime = 0
                with path.open("rb") as source:
                    tar.addfile(info, source)
        built += 1
    print(f"Built {built} .kay archives")

if __name__ == "__main__":
    main()
