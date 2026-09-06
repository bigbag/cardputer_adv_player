#!/usr/bin/env python3
"""Generate the Step-0 audio diagnostic fixture corpus.

All samples are synthesized with the Python standard library. MP3 files are
encoded by ffmpeg with libmp3lame. ffprobe verifies every stream after
writing. The script writes only into --output, never into the repository.

Usage:
    python3.12 tools/generate_audio_fixtures.py --output DIR [--force]
"""

import argparse
import hashlib
import json
import math
import shutil
import struct
import subprocess
import sys
from array import array
from pathlib import Path

MP3_SECONDS = 24.0   # >= 20 s so a +/-5 s seek stays inside the file
WAV_SECONDS = 3.0
BASE_HZ = 1000.0
LEVEL = 0.5          # peak as a fraction of int16 full scale
BLOCK_SECONDS = 0.5  # PCM block size streamed to ffmpeg stdin

# Timed tone bursts so a seek target is identifiable by ear and by spectrum.
MARKERS = {5.0: 2000.0, 10.0: 3000.0, 15.0: 4000.0}
MARKER_SECONDS = 0.5

MP3_SPECS = [  # name, sample rate, channels, libmp3lame args
    ("01_mono_44100.mp3", 44100, 1, ["-b:a", "128k"]),
    ("02_stereo_44100.mp3", 44100, 2, ["-b:a", "128k"]),
    ("03_stereo_48000.mp3", 48000, 2, ["-b:a", "128k"]),
    ("04_cbr_128.mp3", 44100, 2, ["-b:a", "128k"]),
    ("05_cbr_320.mp3", 44100, 2, ["-b:a", "320k"]),
    ("06_vbr_xing.mp3", 44100, 2, ["-q:a", "4"]),
]

WAV_SPECS = [  # name, channels, left mode, right mode; modes: tone/zero/inverted
    ("07_left_only.wav", 2, "tone", "zero"),
    ("08_right_only.wav", 2, "zero", "tone"),
    ("09_lr_same.wav", 2, "tone", "tone"),
    ("10_lr_antiphase.wav", 2, "tone", "inverted"),
    ("11_wav_large_metadata.wav", 1, "tone", None),
]

LIST_PAYLOAD_SIZE = 1024  # pushes the data chunk offset past byte 512
TRUNC_MP3_SRC = "01_mono_44100.mp3"
TRUNC_WAV_SRC = "07_left_only.wav"


def mp3_level(t, vbr):
    """Signed signal level at time t, identical on every channel."""
    for start, hz in MARKERS.items():
        if start <= t < start + MARKER_SECONDS:
            return LEVEL * math.sin(2.0 * math.pi * hz * t)
    if vbr:
        if int(t // 2.0) % 2 == 0:
            # Harmonic stack: dense content so LAME spends bits here.
            return 0.5 * (0.5 * math.sin(2.0 * math.pi * 1000.0 * t)
                          + 0.3 * math.sin(2.0 * math.pi * 2000.0 * t)
                          + 0.2 * math.sin(2.0 * math.pi * 3000.0 * t))
        return 0.05 * math.sin(2.0 * math.pi * BASE_HZ * t)
    return LEVEL * math.sin(2.0 * math.pi * BASE_HZ * t)


def wav_level(t):
    return LEVEL * math.sin(2.0 * math.pi * BASE_HZ * t)


def pcm_blocks(rate, channels, seconds, level_at, channel_gains=None):
    """Yield interleaved s16le byte blocks; bounded memory."""
    if channel_gains is None:
        channel_gains = (1,) * channels
    block_n = int(rate * BLOCK_SECONDS)
    total = int(rate * seconds)
    for start in range(0, total, block_n):
        n = min(block_n, total - start)
        buf = array("h", bytes(n * channels * 2))
        for i in range(n):
            s = int(level_at((start + i) / rate) * 32767.0)
            base = i * channels
            for c in range(channels):
                buf[base + c] = s * channel_gains[c]
        yield buf.tobytes()


def chunk(cid, payload):
    return struct.pack("<4sI", cid, len(payload)) + payload + (b"\x00" if len(payload) % 2 else b"")


def info_payload(total):
    def sub(cid, text):
        return cid + struct.pack("<I", len(text)) + text + (b"\x00" if len(text) % 2 else b"")
    body = (b"INFO"
            + sub(b"IART", b"tools/generate_audio_fixtures.py\x00")
            + sub(b"IGNR", b"1 kHz test tone\x00")
            + sub(b"ICMT", b"oversized LIST chunk so the data chunk starts past byte 512\x00"))
    assert len(body) <= total
    return body + b"\x00" * (total - len(body))


def write_wav(path, rate, channels, seconds, modes, list_size=0):
    """Write a RIFF/WAVE file by hand; returns (data payload offset, data size)."""
    gains = tuple({"tone": 1, "zero": 0, "inverted": -1}[mode] for mode in modes[:channels])
    data_size = int(rate * seconds) * channels * 2
    fmt = chunk(b"fmt ", struct.pack("<HHIIHH", 1, channels, rate,
                                     rate * channels * 2, channels * 2, 16))
    extra = chunk(b"LIST", info_payload(list_size)) if list_size else b""
    data_off = 12 + len(fmt) + len(extra) + 8
    if list_size:
        assert data_off > 512, "metadata chunk failed to push data offset past 512"
    with open(path, "wb") as out:
        out.write(b"RIFF" + struct.pack("<I", 4 + len(fmt) + len(extra) + 8 + data_size) + b"WAVE")
        out.write(fmt)
        out.write(extra)
        out.write(b"data" + struct.pack("<I", data_size))
        for block in pcm_blocks(rate, channels, seconds, wav_level, gains):
            out.write(block)
    return data_off, data_size


def run(cmd):
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"command failed ({proc.returncode}): {' '.join(cmd)}\n{proc.stderr.strip()}")
    return proc.stdout


def encode_mp3(dst, rate, channels, seconds, level_at, codec_args):
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
           "-f", "s16le", "-ar", str(rate), "-ac", str(channels), "-i", "pipe:0",
           "-c:a", "libmp3lame", "-write_xing", "1",
           "-map_metadata", "-1", "-fflags", "+bitexact",
           *codec_args, str(dst)]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        for block in pcm_blocks(rate, channels, seconds, level_at):
            proc.stdin.write(block)
        proc.stdin.close()
    except BrokenPipeError:
        pass
    err = proc.stderr.read().decode("utf-8", "replace")
    if proc.wait() != 0:
        sys.exit(f"ffmpeg failed ({proc.returncode}) for {dst}:\n{err}")


def probe(path):
    meta = json.loads(run(["ffprobe", "-v", "error", "-print_format", "json",
                           "-show_format", "-show_streams", str(path)]))
    stream = next(s for s in meta["streams"] if s.get("codec_type") == "audio")
    return stream, meta.get("format", {})


def id3_size(raw):
    """Size of a leading ID3v2 tag, footer included; 0 when absent."""
    if raw[:3] != b"ID3":
        return 0
    size = ((raw[6] & 0x7F) << 21) | ((raw[7] & 0x7F) << 14) | ((raw[8] & 0x7F) << 7) | (raw[9] & 0x7F)
    size += 10
    if raw[5] & 0x10:  # footer flag
        size += 10
    return size


def xing_info(path):
    """Parse the Xing/Info header of the first MPEG frame; None when absent.

    The leading ID3v2 size is parsed and skipped, so this is not a blind
    string search for b"Xing".
    """
    with open(path, "rb") as f:
        raw = f.read(4096)
    pos = id3_size(raw)
    if pos + 4 > len(raw) or raw[pos] != 0xFF:
        return None
    version = (raw[pos + 1] >> 3) & 3
    mode = (raw[pos + 3] >> 6) & 3
    if version == 3:  # MPEG 1
        side = 17 if mode == 3 else 32
    else:  # MPEG 2 / 2.5
        side = 9 if mode == 3 else 17
    pos += 4 + side
    tag = raw[pos:pos + 4]
    if tag not in (b"Xing", b"Info"):
        return None
    info = {"tag": tag.decode()}
    flags = struct.unpack_from(">I", raw, pos + 4)[0]
    info["flags"] = flags
    if flags & 1:
        info["frames"] = struct.unpack_from(">I", raw, pos + 8)[0]
        info["bytes"] = struct.unpack_from(">I", raw, pos + 12)[0]
    return info


def verify_mp3(path, rate, channels, codec_args, vbr):
    stream, fmt = probe(path)
    expect = {
        "codec_name": "mp3",
        "sample_rate": str(rate),
        "channels": channels,
    }
    for key, want in expect.items():
        got = stream.get(key)
        if got != want:
            sys.exit(f"{path.name}: stream {key} is {got!r}, expected {want!r}")
    if float(fmt.get("duration", 0)) < MP3_SECONDS - 1.0:
        sys.exit(f"{path.name}: duration {fmt.get('duration')} is too short")
    if vbr:
        info = xing_info(path)
        if not info or info.get("tag") != "Xing":
            sys.exit(f"{path.name}: no valid Xing header after the ID3 prefix")
        if info.get("flags", 0) & 3 != 3 or info.get("frames", 0) <= 0 or info.get("bytes", 0) <= 0:
            sys.exit(f"{path.name}: Xing header lacks frames/bytes/TOC fields: {info}")
    else:
        target = int(codec_args[1].rstrip("k")) * 1000
        measured = int(stream.get("bit_rate") or fmt.get("bit_rate") or 0)
        if abs(measured - target) > 0.05 * target:
            sys.exit(f"{path.name}: bit rate {measured} not within 5% of {target}")
    return stream, fmt


def verify_wav(path, rate, channels):
    stream, fmt = probe(path)
    if stream.get("codec_name") != "pcm_s16le" or int(stream.get("sample_rate", 0)) != rate \
            or stream.get("channels") != channels:
        sys.exit(f"{path.name}: unexpected WAV stream properties: {stream}")
    if abs(float(fmt.get("duration", 0)) - WAV_SECONDS) > 0.01:
        sys.exit(f"{path.name}: unexpected WAV duration {fmt.get('duration')}")


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def marker_list():
    return [{"time_s": start, "freq_hz": hz, "duration_s": MARKER_SECONDS}
            for start, hz in sorted(MARKERS.items())]


def make_entry(path, kind, codec, rate, channels, duration, bit_rate, markers, note, truncation):
    return {
        "file": path.name,
        "kind": kind,
        "codec": codec,
        "sample_rate": rate,
        "channels": channels,
        "duration_s": round(duration, 3),
        "bit_rate_bps": bit_rate,
        "bytes": path.stat().st_size,
        "sha256": digest(path),
        "markers": markers,
        "note": note,
        "truncation": truncation,
    }


def truncate_mp3(src, dst, manifest_markers, source_entry):
    # 128 kbit/s at 44100 Hz: frame size 144*128000/44100 ~= 417 bytes.
    # Dropping half a frame from the end always lands inside the last frame.
    raw = src.read_bytes()
    kept = len(raw) - (144 * 128000 // 44100) // 2
    assert kept > 0
    dst.write_bytes(raw[:kept])
    stream, fmt = probe(dst)
    truncation = {"source": src.name, "origin": "mid-frame", "kept_bytes": kept,
                  "note": "final MPEG frame is incomplete"}
    return make_entry(dst, "mp3", "mp3", int(stream["sample_rate"]), stream["channels"],
                      float(fmt.get("duration", 0)), source_entry["bit_rate_bps"],
                      manifest_markers, "truncated copy of " + src.name, truncation)


def truncate_wav(src, dst, data_off, data_size, rate, channels):
    raw = src.read_bytes()
    kept = data_off + (data_size * 2) // 3 + 1
    dst.write_bytes(raw[:kept])
    truncation = {"source": src.name, "origin": "mid-data", "kept_bytes": kept,
                  "note": "RIFF sizes still declare the full length; data ends mid-sample"}
    duration = (kept - data_off) / (rate * channels * 2)
    return make_entry(dst, "wav", "pcm_s16le", rate, channels, duration, None,
                      [], "truncated copy of " + src.name, truncation)


def prepare_output(out, force):
    if out.exists():
        if not out.is_dir():
            sys.exit(f"--output must be a directory: {out}")
        existing = list(out.iterdir())
        if existing and not force:
            sys.exit(f"{out} is not empty ({len(existing)} entries); "
                     f"pass --force to overwrite the fixtures in place (unrelated files are kept)")
    else:
        out.mkdir(parents=True)


def main():
    ap = argparse.ArgumentParser(description="Generate the audio fixture corpus.")
    ap.add_argument("--output", required=True, type=Path, help="target directory")
    ap.add_argument("--force", action="store_true",
                    help="write into a nonempty directory, overwriting only fixture files")
    args = ap.parse_args()

    for tool in ("ffmpeg", "ffprobe"):
        if shutil.which(tool) is None:
            sys.exit(f"{tool} not found on PATH")
    prepare_output(args.output, args.force)
    out = args.output

    version = run(["ffmpeg", "-version"]).splitlines()[0].strip()
    entries = []

    for name, rate, channels, codec_args in MP3_SPECS:
        vbr = "-q:a" in codec_args
        path = out / name
        encode_mp3(path, rate, channels, MP3_SECONDS, lambda t: mp3_level(t, vbr), codec_args)
        stream, fmt = verify_mp3(path, rate, channels, codec_args, vbr)
        measured = int(stream.get("bit_rate") or fmt.get("bit_rate") or 0) or None
        entry = make_entry(path, "mp3", "mp3", rate, channels, float(fmt["duration"]), measured,
                           marker_list(), "cbr" if not vbr else "vbr, content alternates dense/quiet",
                           None)
        entries.append(entry)
        if name == TRUNC_MP3_SRC:
            entries.append(truncate_mp3(path, out / (path.stem + "_truncated.mp3"),
                                        marker_list(), entry))

    for name, channels, left, right in WAV_SPECS:
        list_size = LIST_PAYLOAD_SIZE if "large_metadata" in name else 0
        path = out / name
        data_off, data_size = write_wav(path, 44100, channels, WAV_SECONDS, (left, right), list_size)
        verify_wav(path, 44100, channels)
        note = "mono 1 kHz tone" if right is None else f"left {left}, right {right}"
        entries.append(make_entry(path, "wav", "pcm_s16le", 44100, channels, WAV_SECONDS,
                                  None, [], note, None))
        if name == TRUNC_WAV_SRC:
            entries.append(truncate_wav(path, out / (path.stem + "_truncated.wav"),
                                        data_off, data_size, 44100, channels))

    manifest = {"generator": "tools/generate_audio_fixtures.py",
                "encoder": version,
                "fixtures": entries}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"wrote {len(entries)} fixtures + manifest.json to {out}")


if __name__ == "__main__":
    main()
