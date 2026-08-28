# NuDock

NuDock 2.0 is an OBS Studio plugin that saves explicit Dock Profiles and maps
them to OBS profiles. It restores the intended layout after startup and OBS
profile transitions without continuously fighting temporary dock changes.

## Behavior

Open **Docks > NuDock Profiles...** to:

- create a Dock Profile from the current OBS dock layout;
- rename, delete, load, or explicitly overwrite a Dock Profile;
- assign each OBS profile a Dock Profile or **Keep Current**;
- share one Dock Profile between any number of OBS profiles.

Changes in the manager are staged until **Apply** or **OK**. **Load Now** only
changes the live layout and never changes an assignment. Rearranging docks in
OBS is temporary until **Save Current to Profile** is used.

On startup and profile transitions, NuDock retries restoration on the OBS UI
thread at immediate, short, and late intervals. Each retry is generation
guarded, so rapid switches cannot apply a stale profile. A rejected Qt state is
rolled back to the layout captured immediately before that attempt.

## Storage

NuDock uses OBS's global plugin configuration directory:

```text
plugin_config/nudock/
  config.json
  profiles/<uuid>.json
```

`config.json` contains the schema version and OBS-profile-to-Dock-Profile UUID
assignments. Each profile file contains a stable UUID, unique display name,
creation and update timestamps, OBS version, Qt state version, and base64 dock
state. Files are schema validated and committed with `QSaveFile`.

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

The suite covers serialization, atomic writes, duplicate names, CRUD,
many-to-one mappings, OBS profile rename/delete reconciliation, corrupt and
missing files, and transactional restore rollback.

Compatibility targets are OBS Studio 32.1 and 32.2 on Windows x64. Local OBS
source trees, portable installations, build outputs, packages, DLLs, and PDBs
are testing-only fixtures and remain ignored.

## Repository boundaries

Only NuDock source, locale data, tests, and documentation are versioned.
`deps/`, `artifacts/`, `build/`, and `jr/` are local compatibility/integration
fixtures or generated outputs. The independent NuDock history is pushed to
`origin`; the source project used for reference is available only as the
`upstream` remote.
