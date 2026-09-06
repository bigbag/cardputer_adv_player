#!/usr/bin/env python3
"""Independent regression for the Step-0 audio fixture corpus.

Runs tools/generate_audio_fixtures.py into a temporary directory and verifies
the produced bytes: WAV channel relationships, the RIFF layout of the
large-metadata fixture, manifest hashes and stream properties, CBR/VBR frame
structure, and decoded marker tones. Standard library only; ffmpeg/ffprobe
must be on PATH. Nothing is written into the repository.

Usage:
    python3.12 tools/test_audio_fixtures.py
"""

import hashlib
import json
import math
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
from array import array
from pathlib import Path

GENERATOR = Path(__file__).resolve().parent / "generate_audio_fixtures.py"

# Expected corpus, restated here on purpose so the test does not inherit
# generator bugs through shared constants.
MP3_EXPECT = {
    "01_mono_44100.mp3": (44100, 1),
    "02_stereo_44100.mp3": (44100, 2),
    "03_stereo_48000.mp3": (48000, 2),
    "04_cbr_128.mp3": (44100, 2),
    "05_cbr_320.mp3": (44100, 2),
    "06_vbr_xing.mp3": (44100, 2),
}
CBR_TARGET_BPS = {"04_cbr_128.mp3": 128000, "05_cbr_320.mp3": 320000}
WAV_NAMES = (
    "07_left_only.wav",
    "08_right_only.wav",
    "09_lr_same.wav",
    "10_lr_antiphase.wav",
    "11_wav_large_metadata.wav",
)
TRUNCATED = ("01_mono_44100_truncated.mp3", "07_left_only_truncated.wav")
MARKERS = {5.0: 2000.0, 10.0: 3000.0, 15.0: 4000.0}  # time -> burst frequency
BASE_HZ = 1000
RATE = 44100

CHECKS = []


def check(fn):
    CHECKS.append(fn)
    return fn


def entry(manifest, name):
    found = [e for e in manifest["fixtures"] if e["file"] == name]
    assert len(found) == 1, f"manifest has {len(found)} entries for {name}"
    return found[0]


def goertzel(samples, freq):
    w = 2.0 * math.pi * freq / RATE
    coeff = 2.0 * math.cos(w)
    s1 = s2 = 0.0
    for x in samples:
        s0 = x + coeff * s1 - s2
        s2 = s1
        s1 = s0
    return s1 * s1 + s2 * s2 - coeff * s1 * s2


def read_wav(path):
    with wave.open(str(path), "rb") as w:
        assert w.getsampwidth() == 2 and w.getcomptype() == "NONE", f"{path}: not s16le PCM"
        data = array("h")
        data.frombytes(w.readframes(w.getnframes()))
        return w.getframerate(), w.getnchannels(), w.getnframes(), data


def decode_window(path, start, seconds):
    proc = subprocess.run(
        ["ffmpeg", "-v", "error", "-ss", str(start), "-t", str(seconds),
         "-i", str(path), "-map", "a:0", "-ac", "1", "-ar", str(RATE), "-f", "s16le", "-"],
        capture_output=True)
    assert proc.returncode == 0, f"{path}: decode failed: {proc.stderr.decode()[:300]}"
    samples = array("h")
    samples.frombytes(proc.stdout)
    assert len(samples) >= int(RATE * seconds * 0.8), f"{path}: decode produced too few samples"
    return samples


def dominant_freq(samples, freqs):
    energy = {f: goertzel(samples, f) for f in freqs}
    best = max(energy, key=energy.get)
    runner = max(v for f, v in energy.items() if f != best)
    assert energy[best] > 4.0 * runner, f"no clear dominant frequency: {energy}"
    return best


def id3_skip(raw):
    if raw[:3] != b"ID3":
        return 0
    size = ((raw[6] & 0x7F) << 21) | ((raw[7] & 0x7F) << 14) | ((raw[8] & 0x7F) << 7) | (raw[9] & 0x7F)
    size += 10
    if raw[5] & 0x10:
        size += 10
    return size


V1_BR = (0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320)
V2_BR = (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160)
SR = {0: 44100, 1: 48000, 2: 32000}


def xing_at(raw, pos, version, mode):
    side = (17 if mode == 3 else 32) if version == 3 else (9 if mode == 3 else 17)
    pos += 4 + side
    tag = raw[pos:pos + 4]
    if tag not in (b"Xing", b"Info"):
        return None
    info = {"tag": tag.decode()}
    flags = struct.unpack_from(">I", raw, pos + 4)[0]
    if flags & 1:
        info["frames"] = struct.unpack_from(">I", raw, pos + 8)[0]
        info["bytes"] = struct.unpack_from(">I", raw, pos + 12)[0]
    info["flags"] = flags
    return info


def walk_frames(path):
    """Sequential MPEG frame walk after skipping any ID3v2 prefix."""
    raw = Path(path).read_bytes()
    pos = id3_skip(raw)
    assert pos + 4 <= len(raw) and raw[pos] == 0xFF and (raw[pos + 1] & 0xE0) == 0xE0, \
        f"{path}: no MPEG sync after ID3 prefix"
    xing = None
    bitrates = set()
    frames = 0
    while pos + 4 <= len(raw) and raw[pos] == 0xFF and (raw[pos + 1] & 0xE0) == 0xE0:
        b1, b2, b3 = raw[pos + 1], raw[pos + 2], raw[pos + 3]
        version = (b1 >> 3) & 3
        layer = (b1 >> 1) & 3
        br_idx = (b2 >> 4) & 0xF
        sr_idx = (b2 >> 2) & 3
        pad = (b2 >> 1) & 1
        mode = (b3 >> 6) & 3
        assert layer == 1, f"{path}: frame {frames} is not Layer III"
        assert version in (0, 2, 3), f"{path}: reserved MPEG version bits"
        assert br_idx not in (0, 15), f"{path}: invalid bitrate index at frame {frames}"
        if frames == 0:
            xing = xing_at(raw, pos, version, mode)
        if version == 3:
            br = V1_BR[br_idx]
            size = 144 * br * 1000 // SR[sr_idx] + pad
        else:
            br = V2_BR[br_idx]
            sr = SR[sr_idx] // (2 if version == 2 else 4)
            size = 72 * br * 1000 // sr + pad
        bitrates.add(br)
        pos += size
        frames += 1
    return xing, bitrates, frames


def probe(path):
    proc = subprocess.run(
        ["ffprobe", "-v", "error", "-print_format", "json",
         "-show_format", "-show_streams", str(path)],
        capture_output=True, text=True)
    assert proc.returncode == 0, f"ffprobe failed for {path}: {proc.stderr[:300]}"
    meta = json.loads(proc.stdout)
    stream = next(s for s in meta["streams"] if s.get("codec_type") == "audio")
    return stream, meta.get("format", {})


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


@check
def corpus_present(out, manifest):
    for name in list(MP3_EXPECT) + list(WAV_NAMES) + list(TRUNCATED):
        assert (out / name).is_file(), f"missing fixture {name}"
    assert (out / "manifest.json").is_file()
    assert "encoder" in manifest and manifest["encoder"], "manifest lacks encoder version"
    names = [e["file"] for e in manifest["fixtures"]]
    assert len(names) == 13 and len(set(names)) == 13, f"manifest lists {names}"


@check
def manifest_hashes_match(out, manifest):
    for e in manifest["fixtures"]:
        path = out / e["file"]
        assert path.stat().st_size == e["bytes"], f"{e['file']}: size mismatch"
        assert sha256(path) == e["sha256"], f"{e['file']}: sha256 mismatch"
        if e["truncation"]:
            data = path.read_bytes()
            source = (out / e["truncation"]["source"]).read_bytes()
            assert len(data) < len(source) and source.startswith(data)
            if path.suffix == ".wav":
                assert len(data) % 2 == 1, "truncated WAV must end inside a 16-bit sample"


@check
def mp3_properties_match_manifest(out, manifest):
    for name, (rate, channels) in MP3_EXPECT.items():
        stream, fmt = probe(out / name)
        assert stream["codec_name"] == "mp3", f"{name}: codec {stream['codec_name']}"
        assert int(stream["sample_rate"]) == rate, f"{name}: rate {stream['sample_rate']}"
        assert stream["channels"] == channels, f"{name}: channels {stream['channels']}"
        assert float(fmt["duration"]) >= 20.0, f"{name}: duration {fmt['duration']} < 20 s"
        e = entry(manifest, name)
        assert (e["sample_rate"], e["channels"], e["codec"]) == (rate, channels, "mp3")
        assert e["duration_s"] and e["duration_s"] >= 20.0, f"{name}: manifest duration {e['duration_s']}"
        assert e["markers"], f"{name}: manifest has no seek markers"
        for m in e["markers"]:
            assert MARKERS[m["time_s"]] == m["freq_hz"], f"{name}: marker {m} mismatch"
    for name, target in CBR_TARGET_BPS.items():
        stream, _ = probe(out / name)
        measured = int(stream.get("bit_rate") or 0)
        assert abs(measured - target) <= 0.05 * target, f"{name}: bit rate {measured} vs {target}"
        assert entry(manifest, name)["bit_rate_bps"] == measured, f"{name}: manifest bit rate mismatch"


@check
def wav_channel_identities(out, manifest):
    expect = {
        "07_left_only.wav": "left_active",
        "08_right_only.wav": "right_active",
        "09_lr_same.wav": "identical",
        "10_lr_antiphase.wav": "mirrored",
    }
    for name, kind in expect.items():
        rate, channels, frames, data = read_wav(out / name)
        assert (rate, channels) == (RATE, 2), f"{name}: {rate} Hz, {channels} ch"
        assert frames == int(RATE * 3.0), f"{name}: {frames} frames"
        left = data[0::2]
        right = data[1::2]
        peak = max(max(abs(x) for x in left), max(abs(x) for x in right))
        assert 15000 < peak < 17000, f"{name}: peak {peak} not safely below full scale"
        active = right if kind == "right_active" else left
        window = active[RATE:RATE + 4096]
        assert dominant_freq(window, (700, BASE_HZ, 1300)) == BASE_HZ, f"{name}: tone is not 1 kHz"
        e = entry(manifest, name)
        assert (e["sample_rate"], e["channels"]) == (RATE, 2), f"{name}: manifest mismatch"
        if kind == "left_active":
            assert any(left) and not any(right), f"{name}: right channel is not silent"
        elif kind == "right_active":
            assert not any(left) and any(right), f"{name}: left channel is not silent"
        elif kind == "identical":
            assert left == right, f"{name}: channels differ"
        else:
            assert all(l == -r for l, r in zip(left, right)), f"{name}: channels are not in antiphase"


@check
def wav_large_metadata_layout(out, manifest):
    path = out / "11_wav_large_metadata.wav"
    raw = path.read_bytes()
    assert raw[:4] == b"RIFF" and raw[8:12] == b"WAVE", "not a RIFF/WAVE file"
    pos = 12
    before = []
    data_off = data_size = None
    fmt_params = None
    while pos + 8 <= len(raw):
        cid, size = struct.unpack_from("<4sI", raw, pos)
        if cid == b"fmt ":
            fmt_params = struct.unpack_from("<HHIIHH", raw, pos + 8)
        if cid == b"data":
            data_off, data_size = pos + 8, size
            break
        before.append(cid)
        pos += 8 + size + (size & 1)  # odd chunks carry one pad byte
    assert any(cid in (b"LIST", b"JUNK") for cid in before), f"no large metadata chunk: {before}"
    assert data_off > 512, f"data chunk offset {data_off} is not past byte 512"
    assert data_off + data_size == len(raw), "data chunk does not end at EOF"
    assert fmt_params is not None, "no fmt chunk before data"
    audio_fmt, channels, rate, _, _, bits = fmt_params
    assert (audio_fmt, channels, rate, bits) == (1, 1, RATE, 16), f"fmt params {fmt_params}"
    _, wav_channels, frames, data = read_wav(path)
    assert (wav_channels, frames, len(data)) == (1, data_size // 2, data_size // 2)
    assert entry(manifest, path.name)["channels"] == 1


@check
def cbr_and_vbr_frame_structure(out, manifest):
    for name in ("01_mono_44100.mp3", "02_stereo_44100.mp3", "03_stereo_48000.mp3",
                 "04_cbr_128.mp3", "05_cbr_320.mp3"):
        _, bitrates, frames = walk_frames(out / name)
        assert frames > 700, f"{name}: only {frames} frames walked"
        assert bitrates == ({128} if name != "05_cbr_320.mp3" else {320}), \
            f"{name}: CBR file carries bitrates {bitrates}"
    xing, bitrates, frames = walk_frames(out / "06_vbr_xing.mp3")
    assert xing is not None and xing["tag"] == "Xing", f"06: no Xing header: {xing}"
    assert xing["flags"] & 3 == 3 and xing["frames"] > 0 and xing["bytes"] > 0, \
        f"06: Xing lacks frames/bytes/TOC: {xing}"
    assert frames >= 700, f"06: only {frames} frames walked"
    assert len(bitrates) >= 3, f"06: VBR content did not vary bitrate: {sorted(bitrates)}"


@check
def decoded_frequency_markers(out, manifest):
    assert dominant_freq(decode_window(out / "01_mono_44100.mp3", 2.0, 0.4),
                         (1000, 2000, 3000, 4000)) == 1000, "01: base tone at 2 s is not 1 kHz"
    assert dominant_freq(decode_window(out / "01_mono_44100.mp3", 5.12, 0.26),
                         (1000, 2000, 3000, 4000)) == 2000, "01: marker at 5 s not found"
    assert dominant_freq(decode_window(out / "01_mono_44100.mp3", 10.12, 0.26),
                         (1000, 2000, 3000, 4000)) == 3000, "01: marker at 10 s not found"
    assert dominant_freq(decode_window(out / "06_vbr_xing.mp3", 15.12, 0.26),
                         (1000, 2000, 3000, 4000)) == 4000, "06: marker at 15 s not found"


def main():
    for tool in ("ffmpeg", "ffprobe"):
        if shutil.which(tool) is None:
            sys.exit(f"{tool} not found on PATH")
    with tempfile.TemporaryDirectory(prefix="audio_fixtures_") as td:
        out = Path(td)
        proc = subprocess.run([sys.executable, str(GENERATOR), "--output", str(out)],
                              capture_output=True, text=True)
        if proc.returncode != 0:
            sys.exit(f"generator failed ({proc.returncode}):\n{proc.stderr}")
        manifest = json.loads((out / "manifest.json").read_text())
        failures = []
        for fn in CHECKS:
            try:
                fn(out, manifest)
                print(f"ok   {fn.__name__}")
            except AssertionError as exc:
                failures.append(fn.__name__)
                print(f"FAIL {fn.__name__}: {exc}")
    if failures:
        sys.exit(f"{len(failures)} check(s) failed: {', '.join(failures)}")
    print(f"all {len(CHECKS)} checks passed")


if __name__ == "__main__":
    main()
