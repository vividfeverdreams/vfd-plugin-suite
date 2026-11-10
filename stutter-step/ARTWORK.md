# STUTTER-STEP artwork

## Icon

`Assets/AppIcon.png` is the teal "SS" robot, the sixth member of the suite's mascot family (YB, BS, S, TU, EH). It was made on 9 October 2026 with GPT Image 2.5 Flare in Krea, in edit mode with a transparent background. Inputs: the SHIFTY icon as the edit target, the THROW-UP icon as the reference for two chest letters, and a crop of `Assets/DeviceShell.png` as the color reference. The robot's framing is within 2% of the siblings', so the PNG is used as generated (1024 × 1024 RGBA). JUCE builds `AppIcon.icns` into the VST3 and AU bundles from `ICON_BIG`.

> Use case: precise-object-edit. Asset type: macOS audio plugin app icon, square 1024 x 1024 PNG cutout with a real transparent background. Image 1 is the EDIT TARGET: the green SHIFTY robot mascot. Image 2 is the orange THROW-UP sibling, a STYLE REFERENCE for two chest letters. Image 3 is a COLOR REFERENCE ONLY: the teal translucent acrylic of the new STUTTER-STEP plugin hardware. Create the STUTTER-STEP sibling. Preserve exactly the chunky blocky friendly robot silhouette, the 3D three-quarter front view, pose, proportions, cream slanted pixel eyes on the dark visor, three cream rectangular blocks on the head, cream joints, metallic screws, visible tiny circuit boards under the glossy translucent plastic shell, and the whole robot comfortably inside the frame with the same margins as Image 1. Change all green translucent shell material to luminous aqua-teal sea-glass acrylic matching Image 3 (like a Game Boy Color 'Teal' shell): bright mint-aqua edge highlights, deep petrol-teal thickness at the edges, teal-tinted circuit boards with gold component leads. Replace the single cream 'S' chest lettering with exactly the two letters 'SS' in the same chunky cream pixel-block letter style, centered together on the chest like the two letters in Image 2. No other text. High fidelity manufactured collectible music hardware. Keep the background fully transparent: no background color, no stand, no floor, no drop shadow outside the robot, no watermark.

## Enclosure

`Assets/DeviceShell.png` (1120 × 1408 RGBA) was made with GPT Image 2 in Krea, in edit mode. Inputs: the SHIFTY `DeviceShell.png` as the edit target, with BACK-SCRATCH `Interior-v8.png` and THROW-UP `DeviceShell-v2.png` as style references. The near-black background was then cut to alpha and the color lifted slightly (saturation ×1.18, contrast ×1.04) to match the siblings' vividness. Up to 0.4.3 the editor cropped `(50, 54, 1024, 1304)`, the enclosure's outer edges, so the background showed in the window's rounded corners. Control positions were measured on the result; the measured map is in `BUILD-PLAN.md`.

### Full-bleed window (0.5.0)

The editor now draws `Assets/DeviceShell-fullbleed.png`: the same art with tighter outer corners, made with the suite's `shared/tools/tighten_shell_corners.py` (no pixel moves outside the four corner zones, so every measured position still holds). The source corners are about r 100 (top) and r 140 (bottom). The top half uses radius 30; the bottom half uses radius 60 with the inner 60% of each corner untouched, which keeps the bottom screws and the speaker slots undistorted:

```sh
T=../shared/tools/tighten_shell_corners.py
python3 $T Assets/DeviceShell.png /tmp/top.png    --edges 54.5 54.5 1071.5 1355.5 --radius 100 100 140 140 --new-radius 30
python3 $T Assets/DeviceShell.png /tmp/bottom.png --edges 54.5 54.5 1071.5 1355.5 --radius 100 100 140 140 --new-radius 60 --inner 0.6
python3 -c "import numpy as n;from PIL import Image as I;a=n.array(I.open('/tmp/top.png'));b=n.array(I.open('/tmp/bottom.png'));b[:700]=a[:700];I.fromarray(b).save('Assets/DeviceShell-fullbleed.png',optimize=True)"
```

The window shows `(67, 64, 990, 1259)` of it (`StutterEditor::cropX..cropH`): the smallest rectangle, with 2 px to spare, whose every pixel is fully opaque enclosure. That leaves the rails at the window's edges, as in ENERGY HARVESTER, and no background in the corners at any size. `STUTTER-STEP Shots --window-edges` checks the corners and edge midpoints at every size option. `Assets/DeviceShell.png` stays as the source. The wordmark, every label, value, graph, LED and meter is drawn live by the editor.

Fonts: Chakra Petch SemiBold and Bold Italic, as across the suite (`Assets/ChakraPetch-OFL.txt`).
