"""Find white noise in a game audio dump (CTR_DEV_AUDIO).

Usage: audio_noise.py AUDIO.raw [--window MS] [--flat F] [--min-db DB] [--wav OUT.wav]

The dump is raw 16-bit stereo PCM at 44100 Hz, one VBlank (735 frames) per
block. Each window gets its loudness and spectral flatness (geometric over
arithmetic mean of the power spectrum, 0 for a pure tone, 1 for white noise).
Game music and effects stay under 0.2; white noise scores about 0.58, so
the default threshold is 0.4. Prints the loud, flat windows with their VBlank number (the presented
frame number at 60 FPS), merged into spans, and a total. --wav writes a .wav
copy to listen to.
"""
import argparse
import sys
import wave

import numpy as np

RATE = 44100
VBLANK = RATE // 60


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('raw')
    ap.add_argument('--window', type=int, default=100, help='window length in ms')
    ap.add_argument('--flat', type=float, default=0.4, help='flatness threshold')
    ap.add_argument('--min-db', type=float, default=-40.0, help='ignore windows quieter than this (dBFS)')
    ap.add_argument('--wav')
    a = ap.parse_args()

    pcm = np.fromfile(a.raw, dtype='<i2')
    pcm = pcm[: len(pcm) // 2 * 2].reshape(-1, 2)
    if a.wav:
        with wave.open(a.wav, 'wb') as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(pcm.tobytes())

    mono = pcm.astype(np.float64).mean(axis=1) / 32768.0
    n = RATE * a.window // 1000
    spans = []
    for start in range(0, len(mono) - n + 1, n):
        x = mono[start:start + n]
        rms = np.sqrt(np.mean(x * x))
        db = 20 * np.log10(rms) if rms > 0 else -120.0
        if db < a.min_db:
            continue
        p = np.abs(np.fft.rfft(x * np.hanning(n))) ** 2
        p = p[1:] + 1e-20
        flat = np.exp(np.mean(np.log(p))) / np.mean(p)
        if flat < a.flat:
            continue
        vb = start // VBLANK
        if spans and vb - spans[-1][1] <= (n // VBLANK) + 1:
            spans[-1][1] = vb + n // VBLANK
            spans[-1][2] = max(spans[-1][2], flat)
            spans[-1][3] = max(spans[-1][3], db)
        else:
            spans.append([vb, vb + n // VBLANK, flat, db])

    seconds = len(mono) / RATE
    for s, e, f, d in spans:
        print(f'vblank {s:7d}-{e:7d}  ({s / 60:7.1f}s)  flatness {f:.2f}  peak {d:6.1f} dBFS')
    total = sum(e - s for s, e, _, _ in spans) / 60
    print(f'{len(spans)} noisy spans, {total:.1f}s of {seconds:.1f}s')
    return 0


if __name__ == '__main__':
    sys.exit(main())
