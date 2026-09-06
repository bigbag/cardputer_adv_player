#!/usr/bin/env python3
"""Generate the compact MP3 fixture corpus for the native decoder tests.

Every fixture is a self-generated swept tone (chirp, no copyrighted input),
so each region of the file is acoustically unique: a native seek test can
prove output came from the requested time, not from a periodic repeat or
the file start. Encoded with ffmpeg + libmp3lame, bitexact, no container
metadata except where the name says so. Re-run from the repo root:

    python3 test/fixtures/mp3/generate.py

Truncated and junk variants are NOT stored; tests derive them in memory.
"""

import hashlib
import json
import math
import struct
import subprocess
import sys
from pathlib import Path

LEVEL = 0.5  # peak as a fraction of int16 full scale
F0_HZ = 800.0  # sweep start
F1_HZ = 3200.0  # sweep end

# name, sample rate, channels, seconds, extra ffmpeg args before the output
SPECS = [
    ("cbr128_stereo_44100.mp3", 44100, 2, 2.0, ["-b:a", "128k"]),
    ("cbr128_mono_44100.mp3", 44100, 1, 2.0, ["-b:a", "128k"]),
    ("cbr320_stereo_44100.mp3", 44100, 2, 2.0, ["-b:a", "320k"]),
    ("cbr128_stereo_48000.mp3", 48000, 2, 2.0, ["-b:a", "128k"]),
    ("vbr_stereo_44100.mp3", 44100, 2, 8.0, ["-q:a", "4"]),
    ("noxing_cbr128_44100.mp3", 44100, 2, 2.0, ["-b:a", "128k", "-write_xing", "0"]),
    ("id3v23_cbr128_44100.mp3", 44100, 2, 2.0,
     ["-b:a", "128k", "-id3v2_version", "3",
      "-metadata", "title=fixture", "-metadata", "artist=generate.py"]),
    ("id3v24_cbr128_44100.mp3", 44100, 2, 2.0,
     ["-b:a", "128k", "-id3v2_version", "4",
      "-metadata", "title=fixture", "-metadata", "artist=generate.py"]),
    ("mpeg2_22050_mono.mp3", 22050, 1, 2.0, ["-b:a", "128k"]),
]


def pcm_blocks(rate, channels, seconds):
    """Deterministic linear chirp, closed-form phase per sample."""
    n = int(rate * seconds)
    block = 1024
    for start in range(0, n, block):
        count = min(block, n - start)
        buf = bytearray()
        for i in range(count):
            t = (start + i) / rate
            sweep = (F1_HZ - F0_HZ) / (2.0 * seconds)
            phase = 2.0 * math.pi * (F0_HZ * t + sweep * t * t)
            v = int(LEVEL * 32767.0 * math.sin(phase))
            s = struct.pack("<h", v)
            buf += s * channels
        yield bytes(buf)


def encode(dst, rate, channels, seconds, extra):
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
           "-f", "s16le", "-ar", str(rate), "-ac", str(channels), "-i", "pipe:0",
           "-c:a", "libmp3lame", "-write_xing", "1",
           "-map_metadata", "-1", "-fflags", "+bitexact",
           *extra, str(dst)]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        for block in pcm_blocks(rate, channels, seconds):
            proc.stdin.write(block)
        proc.stdin.close()
    except BrokenPipeError:
        pass
    err = proc.stderr.read().decode("utf-8", "replace")
    if proc.wait() != 0:
        sys.exit(f"ffmpeg failed for {dst}:\n{err}")


def sha256(path):
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def main():
    out = Path(__file__).resolve().parent
    entries = []
    for name, rate, channels, seconds, extra in SPECS:
        dst = out / name
        encode(dst, rate, channels, seconds, extra)
        entries.append({
            "file": name,
            "sample_rate": rate,
            "channels": channels,
            "seconds": seconds,
            "args": extra,
            "sha256": sha256(dst),
            "bytes": dst.stat().st_size,
        })
        print(f"wrote {name} ({dst.stat().st_size} bytes)")
    (out / "manifest.json").write_text(json.dumps({
        "generator": "test/fixtures/mp3/generate.py",
        "note": "chirp 800-3200 Hz, level 0.5, ffmpeg libmp3lame bitexact",
        "fixtures": entries,
    }, indent=2) + "\n")
    print(f"wrote {len(entries)} fixtures + manifest.json")


if __name__ == "__main__":
    main()
