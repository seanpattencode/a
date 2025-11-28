#!/usr/bin/env python3
"""Real-time faster-whisper test with tiny.en beam=5"""
from faster_whisper import WhisperModel
from pasimple import PaSimple, PA_STREAM_RECORD, PA_SAMPLE_S16LE
import numpy as np, time, sys

duration = int(sys.argv[1]) if len(sys.argv) > 1 else 10
print(f"Loading tiny.en model...")
model = WhisperModel("tiny.en", device="cpu", compute_type="int8")

print(f"🎤 Recording {duration}s... speak now!\n")
sample_rate = 16000
chunk_ms = 500  # Update every 500ms
chunk_bytes = int(sample_rate * chunk_ms / 1000) * 2

all_audio = []
last_text = ""

with PaSimple(PA_STREAM_RECORD, PA_SAMPLE_S16LE, 1, sample_rate) as pa:
    end = time.time() + duration
    while time.time() < end:
        data = pa.read(chunk_bytes)
        all_audio.append(np.frombuffer(data, dtype=np.int16))
        
        # Transcribe accumulated audio
        audio = np.concatenate(all_audio).astype(np.float32) / 32768.0
        segments, _ = model.transcribe(audio, beam_size=5, language="en", 
                                        vad_filter=True, without_timestamps=True)
        text = " ".join(s.text for s in segments).strip()
        
        if text != last_text:
            print(f"\r\033[K{text}", end="", flush=True)
            last_text = text

print(f"\n\n✅ Final: {last_text}")
