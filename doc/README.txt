NuDock dock profile manager for OBS Studio
Version 2.1.0
https://github.com/digimbyte/nudock

WHAT NUDOCK DOES

NuDock saves named Dock Profiles and assigns them to OBS profiles. When OBS
starts or changes profile, NuDock restores the assigned dock layout after OBS
finishes loading its interface and plugin docks.

NuDock does not continuously enforce a layout. You can rearrange docks for a
temporary task without altering the saved Dock Profile.

SUPPORTED OBS VERSIONS

- OBS Studio 32.1, Windows x64
- OBS Studio 32.2, Windows x64

INSTALLATION

Close OBS before installing or updating NuDock. Use exactly one layout.

Global plugin layout:

  C:\ProgramData\obs-studio\plugins\nudock\bin\64bit\nudock.dll
  C:\ProgramData\obs-studio\plugins\nudock\data\locale\en-US.ini

Standalone or portable OBS layout:

  <OBS root>\obs-plugins\64bit\nudock.dll
  <OBS root>\data\obs-plugins\nudock\locale\en-US.ini

Start OBS and open Docks > NuDock Profiles.... A successful installation also
logs a line containing:

  [nudock] loaded version 2.1.0

USAGE

Create...
  Captures the current dock layout into a new, uniquely named Dock Profile.

Rename...
  Changes the selected Dock Profile's display name without changing its UUID or
  OBS profile assignments.

Delete
  Deletes the selected Dock Profile and clears every assignment that referred
  to it.

Load Now
  Loads the selected Dock Profile into the current OBS window. This is temporary
  and does not create or change an assignment.

Save Current to Profile
  Explicitly replaces the selected Dock Profile's saved layout with the current
  OBS dock layout.

OBS Profile Assignments
  Choose a Dock Profile for each OBS profile. Multiple OBS profiles may choose
  the same Dock Profile. Choose Keep Current to leave an OBS profile unassigned.
  New OBS profiles start unassigned.

Apply
  Commits staged OBS profile assignments. If the active OBS profile receives a
  new assignment, that Dock Profile loads immediately.

OK
  Applies staged changes and closes the manager.

Cancel
  Discards only staged assignment edits after confirmation. Create, Rename,
  Delete, and Save Current to Profile are already durable. A temporary Load Now
  operation is not reversed.

RESTORE RULES

- Assigned layouts restore after OBS finishes loading and after OBS profile
  changes.
- The full layout is restored once, then dock IDs are polled for five seconds
  without changing the layout.
- Docks created late use targeted restoration. One full fallback is allowed if
  targeted restoration fails.
- Rapid profile changes cancel stale polling and restoration.
- If Qt rejects a saved state, NuDock restores the layout present immediately
  before that attempt.
- Normal dock rearrangement is never saved automatically.

STORAGE

NuDock data is independent of OBS profile and scene-collection directories. OBS
places it under its global plugin configuration directory:

  plugin_config\nudock\config.json
  plugin_config\nudock\profiles\<uuid>.json

Schema v2 uses config.json as an authoritative manifest and transaction commit
point. NuDock writes profile files atomically before publishing the manifest.
Invalid or missing manifested profiles are reported, and unmanifested orphan
files are ignored and later cleaned. Schema v1 data is not imported.

TROUBLESHOOTING

NuDock Profiles... is missing
  Confirm OBS was restarted, both nudock.dll and en-US.ini use the same install
  layout, and the current OBS log does not report a module load failure.

A newly installed plugin dock is not positioned on the first attempt
  Leave the assigned profile active for the five-second late-dock grace window.
  The owning plugin must register the dock with the same stable ID saved in the
  Dock Profile. If the dock was not part of the snapshot, arrange it and use
  Save Current to Profile once.

A temporary arrangement reset after switching OBS profiles
  This is expected for an assigned OBS profile. Use Save Current to Profile to
  replace the saved snapshot, or select Keep Current for that OBS profile.

UNINSTALLATION

Close OBS, then remove nudock.dll and the matching NuDock locale directory from
the selected installation layout. User-created Dock Profiles are stored in the
OBS configuration directory and are not part of the plugin installation.
