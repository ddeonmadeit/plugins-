INSTALLING ON macOS (FL Studio, Logic, Ableton, ...)
====================================================

Quickest, with no macOS warning: one Terminal command
  Open Terminal (Cmd+Space, type Terminal, Return), paste this line and press Return:
      curl -fsSL https://ddeonmadeit.github.io/plugins-/install.sh | bash
  It installs every plug-in, or updates the ones you have (add " -s Sunburst" to pick one).
  Type your Mac password when asked; nothing shows while you type.

Option A: installer package
  1. Double-click the .pkg file.
  2. If macOS says "Apple could not verify ..." (the package is not notarized by Apple):
     click Done, open System Settings > Privacy & Security, scroll down, click "Open Anyway",
     then run the .pkg again.
  3. It installs into /Library/Audio/Plug-Ins/VST3 and /Library/Audio/Plug-Ins/Components
     (plus the sound library in /Library/Application Support/ddeonmadeit for sample-based plug-ins).

Option B: install script (no admin password)
  1. Unzip the .zip.
  2. Open Terminal and run:   bash ~/Downloads/<unzipped folder>/Install.command
     (Double-clicking Install.command also works if macOS lets you; Terminal always works.)
  3. It installs into ~/Library/Audio/Plug-Ins/VST3 and ~/Library/Audio/Plug-Ins/Components
     (plus any "<Plug-in> Library" folder into ~/Library/Application Support/ddeonmadeit)
     and removes the download quarantine flag.

THEN, IN FL STUDIO
  Options > Manage plugins > Find more plugins (or "Start scan").
  The plug-in appears under Generators. Drag it into the Channel Rack.
  If it is not found, make sure the folders above are listed under
  "Plugin search paths" in Manage plugins, then scan again.

CREDITS
  Interface font: Barlow Condensed, Copyright 2017 The Barlow Project Authors,
  SIL Open Font License 1.1 (https://openfontlicense.org).
