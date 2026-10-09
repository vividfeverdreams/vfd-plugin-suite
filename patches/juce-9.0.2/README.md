# JUCE 9.0.2 wrapper patches

Every plugin builds against JUCE 9.0.2 (tag `9.0.2`, commit `72782788ce18c2d4d760b28e0921d6ffc6431102`), checked out at `<plugin>/external/JUCE` and kept out of git.

Two plugins replace one JUCE file, `modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp`, with the copy in their folder here:

| Plugin | Flag | What the hook does |
| --- | --- | --- |
| `back-scratch/` | `BACK_SCRATCH_QUEUE_BRIDGE` | Passes every point of each VST3 parameter queue (not only the last) to `AutomationBridge.h`, so engage/fill automation lands on the right sample. |
| `energy-harvester/` | `ENERGY_HARVESTER_QUEUE_BRIDGE` | Streams the block's parameter queues to `AutomationBridge.h` in order, and delivers zero-length control flushes through `processBlock`. |

Each hook only compiles when its plugin defines the flag. Without it, the file behaves like stock JUCE. The `.diff` next to each copy shows the change against stock 9.0.2 (line endings ignored).

The Windows workflow (`.github/workflows/windows.yml`) clones JUCE 9.0.2 for each plugin and copies these files in. On a Mac, do the same by hand: put a JUCE 9.0.2 checkout at `<plugin>/external/JUCE`, then copy the patched file over the stock one for `back-scratch` and `energy-harvester`.
