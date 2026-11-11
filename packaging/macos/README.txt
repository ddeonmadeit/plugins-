INSTALLING ON macOS (FL Studio, Ableton Live, Logic, ...)
=========================================================

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
  Instruments appear under Generators (drag one into the Channel Rack), effects under Effects.
  If it is not found, make sure the folders above are listed under
  "Plugin search paths" in Manage plugins, then scan again.

OR, IN ABLETON LIVE
  Settings (Cmd+,) > Plug-Ins: turn on "Use Audio Units" and "Use VST3 Plug-In System Folders",
  then click Rescan (hold Option while clicking for a full rescan).
  The plug-in appears in the browser under Plug-Ins, in the ddeonmadeit folder. Drag an
  instrument onto a MIDI track, an effect onto an audio track.
  Key names follow the DAW: Ableton calls middle C "C3" where FL Studio says "C5", and the
  plug-ins show their keys the same way as the piano roll you are using.
  Contour and Hush: transfer and edit in Arrangement View (Session clips have no fixed place
  in the song, so the take can't line up with them).

CREDITS
  Interface font: Instrument Sans, Copyright 2022 The Instrument Sans Project Authors,
  SIL Open Font License 1.1 (https://openfontlicense.org).
