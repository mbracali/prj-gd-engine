#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Decode Guitar Hero (PS2) .vgs multitrack audio to one WAV per channel.

Recovered from SLUS_212.24 (VAGFileReader_New, FUN_0021d7c0 / FUN_0021da70):
  header (0x80 bytes): "VgS!", u32 version (2), then per channel
      {u32 sample_rate, u32 block_count}
  data from 0x80: 16-byte PS-ADPCM blocks; byte 1 & 0x7f is the channel ID
      (it replaces the usual ADPCM flags byte), blocks of a channel appear in order.

Usage: vgs.py info <file.vgs>
       vgs.py wav  <file.vgs> <out_prefix>
"""
import struct
import sys
import wave

# PS-ADPCM prediction filters (numerators over 64).
FILTERS = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]


def read_header(data):
    if data[:4] != b"VgS!":
        raise SystemExit("not a VGS file")
    version = struct.unpack_from("<I", data, 4)[0]
    channels = []
    for i in range(15):
        rate, blocks = struct.unpack_from("<II", data, 8 + i * 8)
        if rate == 0:
            break
        channels.append((rate, blocks))
    return version, channels


def decode_block(block, history):
    shift, filt = block[0] & 0x0F, block[0] >> 4
    f0, f1 = FILTERS[filt] if filt < len(FILTERS) else (0, 0)
    s1, s2 = history
    out = []
    for byte in block[2:]:
        for nibble in (byte & 0x0F, byte >> 4):
            sample = ((nibble ^ 8) - 8) << 12 >> shift
            sample += (s1 * f0 + s2 * f1 + 32) >> 6
            sample = max(-32768, min(32767, sample))
            out.append(sample)
            s2, s1 = s1, sample
    history[:] = [s1, s2]
    return out


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("info", "wav"):
        raise SystemExit(__doc__)
    data = open(sys.argv[2], "rb").read()
    version, channels = read_header(data)
    counts = [0] * len(channels)
    for off in range(0x80, len(data) - 15, 16):
        ch = data[off + 1] & 0x7F
        if ch < len(counts):
            counts[ch] += 1
    print(f"version {version}, {len(channels)} channels")
    for i, ((rate, blocks), seen) in enumerate(zip(channels, counts)):
        print(f"  ch{i}: {rate} Hz, header {blocks} blocks, found {seen}, {blocks * 28 / rate:.1f} s")
    if sys.argv[1] == "info":
        return

    samples = [[] for _ in channels]
    history = [[0, 0] for _ in channels]
    for off in range(0x80, len(data) - 15, 16):
        ch = data[off + 1] & 0x7F
        if ch < len(channels):
            samples[ch].extend(decode_block(data[off:off + 16], history[ch]))
    for i, (rate, _) in enumerate(channels):
        with wave.open(f"{sys.argv[3]}_ch{i}.wav", "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            w.writeframes(struct.pack(f"<{len(samples[i])}h", *samples[i]))


if __name__ == "__main__":
    main()
