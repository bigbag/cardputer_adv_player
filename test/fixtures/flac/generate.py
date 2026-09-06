#!/usr/bin/env python3
"""Generate FLAC fixtures from the waveform used by test_flac_stream.cpp.

Run from the repository root: python3 test/fixtures/flac/generate.py
The native tests create damaged variants in memory.
"""

import hashlib
import json
import math
import struct
import subprocess
from pathlib import Path

SIN_TABLE = [round(32767 * math.sin(2 * math.pi * j / 256)) for j in range(256)]
SPECS = [
    ("chirp16_stereo_44100.flac", 44100, 2, 16, [(800, 3200), (400, 1600)]),
    ("chirp24_mono_32000.flac", 32000, 1, 24, [(600, 2400)]),
]


def samples(rate, bits, sweeps):
    state = [[(start << 64) // rate] * 2 for start, _ in sweeps]
    increments = [((end - start) << 64) // (rate * rate) for start, end in sweeps]
    for frame in range(rate):
        for channel, (phase, delta) in enumerate(state):
            phase = (phase + delta) & ((1 << 64) - 1)
            state[channel] = [phase, (delta + increments[channel]) & ((1 << 64) - 1)]
            value = SIN_TABLE[phase >> 56]
            yield value * 256 + (frame & 127) if bits == 24 else value


def main():
    output = Path(__file__).resolve().parent
    entries = []
    for name, rate, channels, bits, sweeps in SPECS:
        values = list(samples(rate, bits, sweeps))
        raw = b"".join(struct.pack("<i", value << 8) if bits == 24 else
                       struct.pack("<h", value) for value in values)
        packed = b"".join((value & 0xffffff).to_bytes(3, "little") for value in values) if bits == 24 else raw
        path = output / name
        command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                   "-f", "s32le" if bits == 24 else "s16le", "-ar", str(rate),
                   "-ac", str(channels), "-i", "pipe:0", "-c:a", "flac",
                   "-map_metadata", "-1", "-fflags", "+bitexact"]
        if bits == 24:
            command += ["-bits_per_raw_sample", "24"]
        subprocess.run(command + [str(path)], input=raw, check=True)
        data = path.read_bytes()
        assert data[:8] == b"fLaC\x00\x00\x00\x22"
        info = int.from_bytes(data[18:26], "big")
        assert info >> 44 == rate and ((info >> 41) & 7) + 1 == channels
        assert ((info >> 36) & 31) + 1 == bits and info & ((1 << 36) - 1) == rate
        assert int.from_bytes(data[10:12], "big") <= 4608
        assert data[26:42] == hashlib.md5(packed).digest()
        entries.append({"file": name, "sample_rate": rate, "channels": channels,
                        "bits_per_sample": bits, "total_samples": rate,
                        "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)})
        print(f"Generated {name}: {len(data)} bytes")
    (output / "manifest.json").write_text(json.dumps({
        "generator": "test/fixtures/flac/generate.py",
        "note": "Generated chirps. Waveform constants also occur in test_flac_stream.cpp.",
        "fixtures": entries,
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
