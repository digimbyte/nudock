# NuDock

NuDock is a new OBS Studio dock-layout manager fork. The project is being
rebranded and developed independently, with a new Git history beginning in
this repository.

The current plugin source is under `jr/jrdockie`. Its existing directory name
is retained temporarily so the checked-out OBS compatibility environments can
continue loading it while the NuDock rebrand is implemented.

## Repository boundaries

Only the NuDock plugin source, locale data, and documentation are versioned.
The following workspace directories are explicitly local testing infrastructure
and are ignored by Git:

- `deps/` contains OBS source and portable installations used for compatibility
  and integration testing.
- `artifacts/` contains test builds, logs, extracted packages, and validation
  output.
- `build/` contains generated build trees.
- Other entries under `jr/` belong to the inherited OBS plugin integration
  checkout and are not part of NuDock.
- DLL, PDB, library, executable, ZIP, and IDE-generated files are build outputs
  and are not committed.

Release packages will be produced separately from reviewed NuDock source.
