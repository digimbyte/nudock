# NuDock

NuDock 2.1 is an OBS Studio plugin that saves explicit Dock Profiles and maps
them to OBS profiles. It restores the intended layout after startup and OBS
profile transitions without continuously fighting temporary dock changes.

## Behavior

Open **Docks > NuDock Profiles...** to:

- create a Dock Profile from the current OBS dock layout;
- rename, delete, load, or explicitly overwrite a Dock Profile;
- assign each OBS profile a Dock Profile or **Keep Current**;
- share one Dock Profile between any number of OBS profiles.

Create, Rename, Delete, and **Save Current to Profile** are durable immediately.
Only OBS-profile assignments are staged until **Apply** or **OK**; **Cancel**
asks before discarding pending assignment edits. **Load Now** only changes the
live layout and never changes an assignment. Rearranging docks in OBS is
temporary until **Save Current to Profile** is used.

On startup and profile transitions, NuDock applies the complete snapshot once.
For five seconds it polls the registered dock IDs without changing the layout.
A dock that appears late is restored through Qt's targeted dock path, preserving
temporary edits to existing docks. One transactional full fallback is allowed
if targeted restoration fails. Generation guards cancel stale work during rapid
switches, and a rejected Qt state is rolled back to the layout captured before
the attempt.

## Storage

NuDock uses OBS's global plugin configuration directory:

```text
plugin_config/nudock/
  config.json
  profiles/<uuid>.json
```

Schema v2 `config.json` contains an authoritative Dock Profile UUID manifest and
OBS-profile assignments. Each manifested profile contains its stable UUID,
unique display name, timestamps, OBS and Qt state versions, sorted stable dock
IDs, and base64 Qt state. Profile files are atomically written before the config
manifest is published as the transaction commit point. Unmanifested orphan
files are ignored and cleaned later; schema v1 data is not imported.

NuDock does not read, migrate, scan, or delete configuration belonging to any
other plugin.

## Build and test

The plugin source lives at the repository root. Add this directory to an OBS
source checkout as an external plugin and build the `nudock` target. NuDock has
no obs-websocket dependency.

Enable the self-contained Qt test executable with:

```text
-DNUDOCK_BUILD_TESTS=ON
```

The suite covers schema-v2 transactions and orphan handling, strict no-import
behavior, immediate profile persistence, staged mappings, mapped transitions,
rapid-switch cancellation, custom dock layout round-trips, targeted late-dock
restoration, bounded failure handling, and transactional rollback.

Compatibility targets are OBS Studio 32.1 and 32.2 on Windows x64. Local OBS
source trees, portable installations, build outputs, packages, DLLs, and PDBs
are testing-only fixtures and remain ignored.

## Repository boundaries

Only NuDock source, locale data, tests, and documentation are versioned.
`deps/`, `artifacts/`, `build/`, and `jr/` are local compatibility/integration
fixtures or generated outputs. The independent NuDock history is pushed to
`origin`; the source project used for reference is available only as the
`upstream` remote.
