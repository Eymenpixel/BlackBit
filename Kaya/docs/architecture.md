# Kaya and Blackbit runtime contract

This document is the path and trust contract for the Kaya rewrite. Blackbit is
its own kernel. A Linux host, POSIX tools, or an installed shell must never be
assumed by a Blackbit package.

## Package layout

Source repository layout:

```text
Packages/<id>/
  <id>.kay
  README.md
  src/                  reproducible source, when applicable
```

The `.kay` tar archive contains `manifest.json` and the files for its supported
builds. The v2 manifest schema is
[`Kaya/schemas/manifest-v2.schema.json`](../Kaya/schemas/manifest-v2.schema.json).
Each build declares a language/version, a runtime/version, and a relative entry
path. Kaya derives the package ID from the repository directory and requires it
to match the manifest `name`.

## Runtime paths

Paths are resolved through the platform adapter and are relative to the
Kaya-managed data root. Package code must not hard-code a host home directory or
a Linux path.

| Data | Relative path | Access |
| --- | --- | --- |
| Installed payload | `apps/<id>/` | Kaya writes; package reads |
| Per-package writable files | `data/<id>/` | Only that package and Kaya |
| Installed plugin | `plugins/<id>/` | Kaya and the plugin runner |
| Package index/download cache | `cache/` | Kaya only |
| Runtime/language profile | `private/setup.json` | Kaya core only |
| Kaya logs | `logs/` | Kaya writes; diagnostic command reads |

Blackbit's process capability set must expose a package's own payload, its own
data directory, and only the declared kernel commands. It must not expose the
`private/`, `cache/`, or other packages' `data/` directories. The setup profile
must not be passed in argv, environment variables, standard input, or a mounted
path. Same-user mode bits do not provide this boundary; the kernel must enforce
it.

## Private setup profile

Kaya creates `private/setup.json` on first setup and updates the package
selection during installation. The file is versioned and written atomically so
a failed update cannot corrupt the last valid selection. Kaya, not package
code, resolves this data at run time.

The v1 profile schema is
[`Kaya/schemas/setup.schema.json`](../Kaya/schemas/setup.schema.json):

```json
{
  "schemaVersion": 1,
  "runtimeInventory": [
    { "language": "c", "languageVersion": "<selected-version>", "runtime": "<runtime-name>", "runtimeVersion": "<runtime-version>", "runtimeId": "<Blackbit runtime ID>" }
  ],
  "selectedBuilds": {
    "<package-id>": { "buildId": "<build-id>", "language": "c", "languageVersion": "<selected-version>", "runtime": "<runtime-name>", "runtimeVersion": "<runtime-version>" }
  }
}
```

The values in angle brackets are filled from Blackbit's runtime inventory; they
are not defaults. Package manifests carry supported language/runtime versions.
The current POSIX development backend parses up to 16 v2 build variants and
records the explicit `default` variant, or the first variant when none is
marked. It does not yet enumerate Blackbit runtimes or prompt the user to
choose among compatible variants. The Blackbit adapter must intersect the
manifest builds with the kernel runtime inventory and fail installation when
there is no compatible build.

## Command API and permissions

Blackbit exposes command families such as `peek`/`scribe` for files,
`treeview`/`enter` for directories, `machine`/`ramview` for system information,
`tasks`/`freeze` for process management, `netview`/`listen` for networking,
`citizen`/`grant` for account administration, `mountdisk`/`checkdisk` for
storage, and `forge` for package operations. The command names are part of the
Blackbit interface; exact argument parsing and result/error formats are defined
by the kernel ABI.

Every package declares the command capabilities it needs. Kaya grants only
those capabilities at launch. Read-only packages do not receive write,
process-control, network, user-administration, disk-formatting, or package
installation capabilities. `erase`, `wipe`, `formatdisk`, `partition`,
`grant`, `revoke`, and network scanning/listening require explicit interactive
confirmation and are never run as install hooks.

## Special packages

### small-creatures

The package owns only `data/small-creatures/`. It renders a menu with Customize,
Saves, Reset, Help, and About. `--customize <background-hex|E>
<text-hex|E>` updates terminal colors; `E` preserves that color. `--save <n>`
stores the current theme in the private package data directory. Save/load/reset
must validate profile IDs and never write outside that directory.

### agent-switcher

This package is an adapter dispatcher, not a universal binary installer. It
detects only registered backends and asks the platform adapter to install a
backend. It must not run an arbitrary downloaded installer or request elevated
access without showing the exact action and obtaining confirmation. Blackbit's
own backend is `forge`; external managers such as APT are available only on
platforms that explicitly advertise them.

### nexus

Nexus is a VM front end. It delegates machine creation and execution to a
Blackbit virtualization plugin/API, checks that the requested resources fit
available limits, and requires explicit permission before creating disk images
or attaching devices. It must not silently invoke host virtualization tools.

## Platform adapter boundary

Kaya core owns package metadata, dependency resolution, path validation,
installation transactions, setup profile selection, and permission policy.
Platform adapters own console I/O, process creation, storage primitives,
runtime discovery, command capability grants, and optional networking. The
POSIX source in `Kaya/src` is a development backend only. The Blackbit adapter
must implement the same core contract using the native ABI supplied by
Blackbit; it must not shell out to Linux utilities.

## Implementation status

The v2 package manifest parser and private profile storage are implemented in
the POSIX development backend. Build selection currently uses a deterministic
default and does not discover installed runtimes. The POSIX sandbox does not
implement Blackbit command capability enforcement. Native Blackbit runtime
discovery, interactive language selection, capability grants, and the kernel
ABI adapter remain required before packages can be described as
Blackbit-native. Legacy `.kyp` plugins remain supported until v2 runtime
adapters are available.
