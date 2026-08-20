#!/usr/bin/env python3
"""
r2d2_sound_server.py -- runs on the Pi. Listens on a TCP port for short
text commands ("BEEP", "BEEP happy", ...) and plays a synthesized R2D2-style
beep/boop/warble through the Pi's speaker. Tones are generated on the fly
with numpy (no sound files needed) and piped as raw PCM to `aplay`.

Independent of llama_teleop_server.py -- different port, no shared camera
or serial resources, safe to run alongside it.

  python3 r2d2_sound_server.py
  python3 r2d2_sound_server.py --port 8092 --volume 0.5
"""

import argparse
import random
import socket
import subprocess
import threading

import numpy as np

DEFAULT_SOUND_PORT = 8092
SAMPLE_RATE = 44100


# ------------------------------------------------------------ synthesis

def _tone(freq_start, freq_end, dur, sample_rate=SAMPLE_RATE):
    """One sine sweep from freq_start to freq_end over dur seconds."""
    n = max(1, int(dur * sample_rate))
    t = np.linspace(0, dur, n, endpoint=False)
    freq = np.linspace(freq_start, freq_end, n)
    phase = 2 * np.pi * np.cumsum(freq) / sample_rate
    wave = np.sin(phase)
    # short fade in/out to avoid clicks between tones
    fade = min(n // 8, int(0.01 * sample_rate))
    if fade > 0:
        env = np.ones(n)
        env[:fade] = np.linspace(0, 1, fade)
        env[-fade:] = np.linspace(1, 0, fade)
        wave *= env
    return wave


def _sequence(specs):
    """specs: list of (freq_start, freq_end, dur). Returns one float array."""
    return np.concatenate([_tone(a, b, d) for a, b, d in specs])


def make_random(rng):
    n = rng.randint(3, 7)
    specs = []
    freq = rng.uniform(400, 1800)
    for _ in range(n):
        nxt = freq * rng.uniform(0.5, 2.0)
        nxt = max(250, min(3500, nxt))
        dur = rng.uniform(0.05, 0.16)
        specs.append((freq, nxt, dur))
        freq = nxt
    return _sequence(specs)


def make_happy(rng):
    base = rng.uniform(500, 900)
    specs = []
    for i in range(rng.randint(4, 6)):
        step = base * (1.15 ** i)
        specs.append((step, step * rng.uniform(1.3, 1.8), 0.09))
    return _sequence(specs)


def make_sad(rng):
    base = rng.uniform(900, 1300)
    specs = []
    for i in range(rng.randint(3, 5)):
        step = base * (0.75 ** i)
        specs.append((step * 1.4, step * 0.8, 0.22))
    return _sequence(specs)


def make_warble(rng):
    specs = []
    for _ in range(rng.randint(6, 10)):
        a = rng.uniform(600, 2200)
        b = rng.uniform(600, 2200)
        specs.append((a, b, rng.uniform(0.03, 0.06)))
    return _sequence(specs)


def make_whistle(rng):
    a = rng.uniform(400, 800)
    b = rng.uniform(1800, 3200)
    return _sequence([(a, b, rng.uniform(0.5, 0.9))])


SOUND_MAKERS = {
    "random": make_random,
    "happy": make_happy,
    "sad": make_sad,
    "warble": make_warble,
    "whistle": make_whistle,
}


def synthesize(name, seed=None):
    rng = random.Random(seed)
    maker = SOUND_MAKERS.get(name, make_random)
    wave = maker(rng)
    wave = np.clip(wave, -1.0, 1.0)
    return wave


# ------------------------------------------------------------- playback

class Player:
    """Serializes playback so overlapping BEEP commands don't garble aplay."""

    def __init__(self, volume):
        self.volume = volume
        self.lock = threading.Lock()

    def play(self, name, seed=None):
        wave = synthesize(name, seed) * self.volume
        pcm = (wave * 32767).astype("<i2").tobytes()
        with self.lock:
            try:
                subprocess.run(
                    ["aplay", "-q", "-t", "raw", "-f", "S16_LE",
                     "-c", "1", "-r", str(SAMPLE_RATE)],
                    input=pcm, check=False,
                )
            except FileNotFoundError:
                print("aplay not found -- install alsa-utils to hear sound")


# ------------------------------------------------------------ networking

def handle_conn(conn, addr, player):
    print(f"client connected from {addr[0]}:{addr[1]}")
    buf = b""
    try:
        while True:
            chunk = conn.recv(4096)
            if not chunk:
                break
            buf += chunk
            *complete, buf = buf.split(b"\n")
            for raw in complete:
                line = raw.decode(errors="ignore").strip()
                if not line:
                    continue
                parts = line.split()
                cmd = parts[0].upper()
                if cmd == "BEEP":
                    name = parts[1].lower() if len(parts) > 1 else "random"
                    print(f"playing: {name}")
                    threading.Thread(target=player.play, args=(name,),
                                      daemon=True).start()
    except (ConnectionResetError, OSError):
        pass
    finally:
        conn.close()
        print(f"client {addr[0]}:{addr[1]} disconnected")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=DEFAULT_SOUND_PORT)
    ap.add_argument("--volume", type=float, default=0.7,
                     help="0.0 - 1.0")
    args = ap.parse_args()

    player = Player(max(0.0, min(1.0, args.volume)))

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.host, args.port))
    sock.listen(4)
    print(f"r2d2 sound server listening on {args.host}:{args.port}")
    print(f"sounds: {', '.join(SOUND_MAKERS)}")

    try:
        while True:
            conn, addr = sock.accept()
            threading.Thread(target=handle_conn, args=(conn, addr, player),
                              daemon=True).start()
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()


if __name__ == "__main__":
    main()
