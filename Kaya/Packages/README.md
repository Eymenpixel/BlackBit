# Kaya package catalog

This directory contains distributable Kaya applications. A `.kay` file is a
tar archive containing `manifest.json` and the package program. C++ source
packages use the `cpp` runtime plugin to compile their entry point at launch.
A package belongs here only when it adds an application-level job that Blackbit's built-in
commands do not already perform. Do not ship wrappers that simply rename
`peek`, `scribe`, `treeview`, `machine`, `tasks`, `netview`, `forge`, or another
kernel command.

## Curation rules

- Give every package one clear primary job and a unique package ID.
- Allow at most two packages for the same primary job, and explain the real
  difference in their descriptions.
- Reuse Blackbit's built-in command interface for system operations instead of
  bundling POSIX utilities or assuming a Linux host.
- Declare only the capabilities the application needs. Destructive or
  privileged operations must ask the user at the moment they are performed.
- Include source, a reproducible build recipe, a manifest, and a `.kay`
  archive together in each package directory.
- Put the declared program entry inside the archive. C++ manifests declare
  `runtime: cpp`, `language: cpp`, and the required `cpp` plugin. Keep source
  and build instructions beside the distributable.
- Mark an application Blackbit-native only after its entry format and API calls
  have been checked against the Blackbit ABI.

## First-party applications

- `small-creatures`: terminal theme editor with HEX colors and save slots.
- `agent-switcher`: selects manager adapters and maps `dpkg` to `apt`.
- `nexus`: runs programs in its own portable bytecode VM.

## Community catalog

The repository currently ships 197 catalog utilities in addition to the three
first-party applications (200 `.kay` packages total). The catalog covers text
formatting, encodings, number helpers, developer aids, data transforms, and
generators. Most catalog packages share a small C++ dispatcher header and have
their own entry point, manifest, and reproducible archive. This is a curated
starter catalog; the POSIX backend tests compilation, archive contents, and a
set of representative behaviors. It does not establish compatibility with
Blackbit's native ABI.

Regenerate the catalog and archives, then run the WSL smoke suite:

```sh
python3 tools/catalog.py
python3 tools/build_packages.py
python3 tools/test_packages.py
```

The native `.kay` archives and C++ source files are kept together in each
package folder. The POSIX C++ runtime plugin is a development backend; native
Blackbit runtime support still requires its kernel adapter.
