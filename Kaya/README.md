# Kaya

Kaya is the package manager and application layer for Blackbit. This repository
holds the Kaya core, packages, plugins, and the contracts that let Blackbit
select and run the right package build for its configured language runtime.

## Repository layout

```text
Kaya/                  Kaya core and package tooling
Packages/<package>/    One distributable .kay archive per package
Plugins/<plugin>/      One distributable .kyp archive per plugin
docs/                  Runtime, package, and security contracts
```

Each `.kay` archive contains `manifest.json` and its declared build entries.
Package source and its build instructions stay beside the archive so
maintainers can reproduce it. The v2 package schema supports several language
and runtime builds for one package. A `.kyp` archive contains `plugin.json`, a
runner entry point, and any plugin support files.

## Blackbit package paths

Kaya resolves all paths from its configured data root, rather than assuming a
Linux home directory:

| Purpose | Path relative to Kaya data root |
| --- | --- |
| Installed package payload | `apps/<package-id>/` |
| Package writable data | `data/<package-id>/` |
| Installed plugin payload | `plugins/<plugin-id>/` |
| Download and index cache | `cache/` |
| Private runtime profile | `private/setup.json` |

`private/setup.json` belongs to Kaya core. It stores the selected language and
runtime version for each installed package. Package processes must never receive
the file path, a copy of its contents, or a capability to open it. Blackbit must
enforce that boundary in its process/filesystem capabilities; file mode bits
alone are not a security boundary between programs running as the same user.
Packages receive only their own payload and explicitly granted writable data.

The setup/runtime contract and current implementation limits are documented in
[docs/architecture.md](docs/architecture.md). Package and private setup profile
schemas are in `Kaya/schemas/`.

## Blackbit command surface

Packages may use the Blackbit command API for file and folder operations,
system/process inspection, networking, user and disk management, and `forge`
package operations. Each package must declare the commands and capabilities it
needs. Destructive operations such as `wipe`, `formatdisk`, `erase`, and user or
network administration require explicit user confirmation and must not be
silently run during installation.

## Packages and plugins

Packages are versioned applications. Plugins provide language/runtime adapters
or other narrowly scoped integration points. Package manifests declare
language and runtime versions, entry points, and dependencies. Kaya records the
selected build in the private setup profile; native Blackbit runtime discovery
and compatible-build prompting remain part of the kernel adapter work.

The first-party packages are `small-creatures` (terminal themes),
`agent-switcher` (platform-supported package manager adapters), and `nexus`
(a portable bytecode VM). The repository includes 200 `.kay` packages in total,
including 197 catalog utilities. Their current specifications are in
[`Packages/README.md`](Packages/README.md); executable Blackbit packages need
the native ABI/runtime adapter first.

## Building Kaya

The current C source has a POSIX development backend for local development. The
Blackbit backend must use Blackbit's native command/process and storage APIs;
POSIX host tools are not treated as Blackbit kernel facilities.

```sh
make -C Kaya
```

See [docs/architecture.md](docs/architecture.md) for the package/runtime path
contract and platform boundary.
