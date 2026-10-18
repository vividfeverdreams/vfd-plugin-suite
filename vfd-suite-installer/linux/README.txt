VIVID FEVER DREAMS SUITE for Linux (beta)

Six VST3 plug-ins for 64-bit Linux (x86_64):

  YOI-BOI            talking bass synth (an instrument)
  THROW-UP           mid-bass reverb throws
  BACK-SCRATCH       tempo-synced scratch patterns
  SHIFTY             formant shifter for bass
  ENERGY HARVESTER   tape start, stop and brake
  STUTTER-STEP       swing gate with slap delay

To install, open a terminal in this folder and run:

  ./install.sh

That copies all six into ~/.vst3, the VST3 folder for your user, which REAPER, Bitwig Studio, Ardour, Carla and other VST3 hosts search. To pick some: ./install.sh SHIFTY YOI-BOI. To install for every user: sudo ./install.sh --system (puts them in /usr/local/lib/vst3). Running it again updates them; ./install.sh --uninstall removes them. You can also copy the .vst3 folders from VST3/ by hand.

After installing, open your DAW and rescan plug-ins. If the DAW was open, restart it to load the new versions.

Needs a desktop with X11 or XWayland and glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36 or later, and current Arch, openSUSE and Mint).

The Linux, Windows and Mac versions are the same plug-ins, so a project saved on one opens with the same settings on the others. Your own presets live in ~/.config/<plug-in name>/Presets.

Each plug-in's INFO page shows the suite version and lights up when a newer suite is out; GET opens its download page. The check uses libcurl, which almost every Linux desktop has; without it the plug-ins work as usual and only the check is skipped.

Source code (free software, GNU AGPLv3): https://github.com/vividfeverdreams/vfd-plugin-suite

Executive producers: C.Y. Lee (@cxy) and Carl Tydingco (@clt)
https://www.vividfeverdreams.xyz
