JrDockie dock layout manager for OBS Studio
v1.6.2 (2026-08-25)
by Jesse Reichler aka dcmouser <jessereichler@gmail.com>
https://github.com/dcmouser/obsPlugins/tree/main/jr

WHAT JRDOCKIE DOES

JrDockie saves and restores the state and positions of OBS dock panels. Dock
sets make it easy to switch between layouts used by different profiles,
workflows, or plugins.

SUPPORTED OBS VERSIONS

- OBS Studio 32.1.0, Windows x64, Qt 6.8.3
- OBS Studio 32.2.2, Windows x64, Qt 6.11.1

The included DLL was built against OBS 32.1.0, the lowest supported version,
and validated with both versions above.

PACKAGE CONTENTS

jrDockie.zip contains both installation layouts under purpose-named folders:

  jrDockie.zip
  |-- README.txt
  |-- Option A - Global\
  |   `-- jrdockie\
  |       |-- bin\64bit\jrdockie.dll
  |       `-- data\locale\en-US.ini
  `-- Option B - Standalone\
      |-- obs-plugins\64bit\jrdockie.dll
      `-- data\obs-plugins\jrdockie\locale\en-US.ini

jrdockie.dll is the 64-bit OBS plugin. en-US.ini contains the English menu
labels and error messages used by OBS.

Both options use the same tested DLL. OBS changes the module search paths for
global versus standalone plugins; it does not require a different plugin ABI.

The data directory only contains en-US.ini because JrDockie has no bundled
images, presets, or default dock layouts. This is intentional. User-created
.dockset files are generated after installation and are stored in the OBS user
configuration directory, not in the plugin package.

INSTALLATION

1. Close OBS completely, including any OBS icon still running in the Windows
   notification area.
2. Open jrDockie.zip and choose one folder:
   - Option A - Global: extract its jrdockie folder directly into
     C:\ProgramData\obs-studio\plugins.
   - Option B - Standalone: extract its data and obs-plugins folders directly into
     <OBS root>, the root directory of the installed or portable OBS copy.
     The completed paths must be:

       <OBS root>\obs-plugins\64bit\jrdockie.dll
       <OBS root>\data\obs-plugins\jrdockie\locale\en-US.ini
3. Start OBS and open Docks > Dock Sets.

Administrator permission may be required for Program Files or ProgramData.
OBS does not load the ZIP itself; extract the selected layout before starting
OBS. Do not install both layouts.

UPDATING

1. Close OBS completely.
2. Replace the existing jrdockie.dll and en-US.ini using the same installation
   layout chosen above.
3. Start OBS and confirm the log reports JrDockie v1.6.2.

Existing v1.6.2 .dockset files are stored outside the plugin directory and are
not removed or overwritten by an update.

VERIFYING THE INSTALLATION

Start OBS and open Docks. Dock Sets should appear immediately below Reset
Docks. JrDockie also adds Tools > JrDockie: Dock Sets as a fallback entry.

To confirm the loaded version:

1. Open Help > Log Files > View Current Log.
2. Search for jrdockie.
3. A successful load reports:

   [jrdockie] loaded version 1.6.2 for OBS 32.x.x

USAGE

Open Docks > Dock Sets and select:

- Save Current Dock Set As...
  Saves the current OBS dock state and positions.
- Load Dock Set From File...
  Loads a .dockset from any selected location.
- A saved dock-set name
  Loads that managed dock set immediately.
- Open Dock Set Folder
  Opens the managed dock-set storage directory.

The hotkey named "JrDockie: Cycle Dock Sets" cycles through managed dock sets
in filename order. Assign the key combination in Settings > Hotkeys.

DOCK-SET STORAGE

For a normal OBS installation, managed files are stored under:

  %APPDATA%\obs-studio\plugin_config\jrdockie\docksets

For portable OBS, managed files are stored under:

  <portable OBS>\config\obs-studio\plugin_config\jrdockie\docksets

Each .dockset is versioned JSON containing the OBS version and the serialized
Qt main-window dock state. Older unversioned dockset files are not imported;
save new layouts with v1.6.2.

OBS-WEBSOCKET VENDOR REQUESTS

Vendor name: jrDockie

LoadDockset
  Request data: {"filename":"name-or-full-path"}
  Response data: {"accepted":true,"path":"..."} or
                 {"accepted":false,"error":"Dock set not found"}

CycleDockset
  Request data: {}
  Response data: {"accepted":true}

TROUBLESHOOTING

- Dock Sets is missing:
  Confirm OBS was restarted after installation and search the current OBS log
  for jrdockie or "module failed to open".
- OBS reports module failed to open:
  Confirm that OBS is 64-bit and version 32.1.0 or 32.2.2, then reinstall both
  the DLL and locale file from this package.
- Menu labels are blank or incorrect:
  Reinstall en-US.ini in the matching data path shown above.
- A plugin dock is absent after loading a dock set:
  Load the plugin that creates that dock first, then load the dock set again.
- A layout was saved but is not listed:
  Select Open Dock Set Folder and confirm the file has a .dockset extension.

UNINSTALLATION

Close OBS, then remove jrdockie.dll and the jrdockie locale directory from the
installation layout used above. User-created .dockset files remain in the OBS
user configuration directory unless removed separately.
