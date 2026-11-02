# JUCE 9.0.2 wrapper patches

Every plugin builds against JUCE 9.0.2 (tag `9.0.2`, commit `72782788ce18c2d4d760b28e0921d6ffc6431102`), checked out at `<plugin>/external/JUCE` and kept out of git.

Two plugins replace one JUCE file, `modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp`, with the copy in their folder here:

| Plugin | Flag | What the hook does |
| --- | --- | --- |
| `back-scratch/` | `BACK_SCRATCH_QUEUE_BRIDGE` | Passes every point of each VST3 parameter queue (not only the last) to `AutomationBridge.h`, so engage/fill automation lands on the right sample. |
| `energy-harvester/` | `ENERGY_HARVESTER_QUEUE_BRIDGE` | Streams the block's parameter queues to `AutomationBridge.h` in order, and delivers zero-length control flushes through `processBlock`. |

Each hook only compiles when its plugin defines the flag. Without it, the file behaves like stock JUCE. The `.diff` next to each copy shows the change against stock 9.0.2 (line endings ignored).

The Windows and Linux workflows (`.github/workflows/windows.yml`, `linux.yml`) clone JUCE 9.0.2 for each plugin and copy these files in. On a Mac or a Linux computer, do the same by hand: put a JUCE 9.0.2 checkout at `<plugin>/external/JUCE`, then copy the patched file over the stock one for `back-scratch` and `energy-harvester`.

## Linux: popup menus in out-of-process hosts

`linux/modules/` holds two more changed JUCE files, used by **every** plugin's Linux build (`linux.yml` copies them over `<plugin>/external/JUCE/modules/`). They fix [issue #2](https://github.com/vividfeverdreams/vfd-plugin-suite/issues/2), reported with this fix by RufusIbiza (upstream: [JUCE #1781](https://github.com/juce-framework/JUCE/issues/1781)): in hosts that run plug-ins in a helper process, such as Bitwig, preset menus closed as soon as the mouse went into a submenu, so nothing could be picked.

| File | Change |
| --- | --- |
| `juce_gui_basics/detail/juce_WindowingHelpers.h` | On Linux and BSD a plug-in view counts as embedded in the foreground process. JUCE's Linux version always said no, and in an out-of-process host the plug-in's own process never has the X11 focus. Windows and macOS keep stock JUCE. |
| `juce_gui_basics/menus/juce_PopupMenu.cpp` | A menu that thinks the app lost focus closes only when the mouse is off the whole menu, submenus included (`isOverAny`), not when it leaves the one window checking. |

Side effects on Linux: tooltips also show while the host window isn't focused, and an open menu no longer closes by itself when you switch to another app (a click outside it still closes it). `juce_popup_menus_linux.diff` shows both changes against stock 9.0.2 (line endings ignored). For a Linux build by hand, also copy `linux/modules/` over `<plugin>/external/JUCE/modules/`. Mac and Windows builds don't use these files.
