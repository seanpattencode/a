#!/usr/bin/env python3
"""Real-time faster-whisper test with tiny.en beam=5
Press 'q', Escape, or Ctrl+C to stop cleanly and instantly."""

from faster_whisper import WhisperModel
from pasimple import PaSimple, PA_STREAM_RECORD, PA_SAMPLE_S16LE
import numpy as np
import time
import sys
import select
import termios
import tty
import signal

# Global state for clean shutdown
_stop = False
_term_settings = None

def _on_signal(sig, frame):
    global _stop
    _stop = True

def _check_key():
    """Non-blocking key check - returns True if quit key pressed"""
    global _stop
    if select.select([sys.stdin], [], [], 0)[0]:
        key = sys.stdin.read(1)
        if key in ('q', 'Q', '\x1b'):  # q, Q, or Escape
            _stop = True
    return _stop

def main():
    global _stop, _term_settings

    duration = int(sys.argv[1]) if len(sys.argv) > 1 else 10

    print("Loading tiny.en model...")
    model = WhisperModel("tiny.en", device="cpu", compute_type="int8")

    print(f"🎤 Recording {duration}s... (q/Esc/Ctrl+C to stop)\n")

    sample_rate = 16000
    chunk_ms = 150  # Small chunks for ~150ms response time
    chunk_bytes = int(sample_rate * chunk_ms / 1000) * 2

    all_audio = []
    last_text = ""

    # Setup signal handlers
    signal.signal(signal.SIGINT, _on_signal)
    signal.signal(signal.SIGTERM, _on_signal)

    # Set terminal to cbreak mode (immediate key detection, no echo)
    if sys.stdin.isatty():
        _term_settings = termios.tcgetattr(sys.stdin)
        tty.setcbreak(sys.stdin.fileno())

    try:
        with PaSimple(PA_STREAM_RECORD, PA_SAMPLE_S16LE, 1, sample_rate) as pa:
            end = time.time() + duration
            while time.time() < end and not _stop:
                _check_key()
                if _stop:
                    break

                data = pa.read(chunk_bytes)
                all_audio.append(np.frombuffer(data, dtype=np.int16))

                audio = np.concatenate(all_audio).astype(np.float32) / 32768.0
                segments, _ = model.transcribe(
                    audio, beam_size=5, language="en",
                    vad_filter=True, without_timestamps=True
                )
                text = " ".join(s.text for s in segments).strip()

                if text != last_text:
                    print(f"\r\033[K{text}", end="", flush=True)
                    last_text = text
    finally:
        # Restore terminal
        if _term_settings:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, _term_settings)

    print("\n")
    if _stop:
        print("⏹️  Stopped")
    print(f"✅ Final: {last_text}")

if __name__ == "__main__":
    main()
