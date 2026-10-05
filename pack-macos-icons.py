"""Package approved PNG representations using JUCE's PNG-based ICNS layout."""
from pathlib import Path
import struct
import sys

root = Path(sys.argv[1])
representations = {
    "icp4": "icon_16x16.png",
    "icp5": "icon_32x32.png",
    "icp6": "icon_32x32@2x.png",
    "ic07": "icon_128x128.png",
    "ic08": "icon_256x256.png",
    "ic09": "icon_512x512.png",
    "ic10": "icon_512x512@2x.png",
}
for slug in ("yoi-boi", "shifty", "back-scratch"):
    chunks = []
    for kind, filename in representations.items():
        data = (root / slug / "AppIcon.iconset" / filename).read_bytes()
        assert data[:8] == b"\x89PNG\r\n\x1a\n"
        chunks.append(kind.encode("ascii") + struct.pack(">I", len(data) + 8) + data)
    payload = b"".join(chunks)
    destination = root / slug / "AppIcon.icns"
    destination.write_bytes(b"icns" + struct.pack(">I", len(payload) + 8) + payload)
    print(f"{slug}: packaged seven PNG representations, 16–1024px, {destination.stat().st_size} bytes")
