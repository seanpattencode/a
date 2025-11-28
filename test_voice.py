#!/usr/bin/env python3
"""
Voice transcription test file - compare multiple methods
Usage:
  python test_voice.py file <audio.wav>     # Test with audio file
  python test_voice.py mic <duration>       # Test with microphone (default 5s)
  python test_voice.py mic 10 whisper       # Specific method

Methods: sherpa, whisper, vosk
"""
import sys, os, time

# ============== METHOD 1: SHERPA-ONNX (streaming) ==============
def test_sherpa_file(audio_path):
    """Test sherpa-onnx with audio file"""
    print("\n[SHERPA-ONNX] Testing with file...")
    try:
        import sherpa_onnx
        import wave
        import numpy as np

        model_dir = os.path.expanduser('~/.local/share/sherpa-onnx/sherpa-onnx-streaming-zipformer-en-2023-06-26')
        if not os.path.exists(model_dir):
            print("  ERROR: Model not found. Run: aio voice (to auto-download)")
            return None

        recognizer = sherpa_onnx.OnlineRecognizer.from_transducer(
            tokens=f'{model_dir}/tokens.txt',
            encoder=f'{model_dir}/encoder-epoch-99-avg-1-chunk-16-left-128.onnx',
            decoder=f'{model_dir}/decoder-epoch-99-avg-1-chunk-16-left-128.onnx',
            joiner=f'{model_dir}/joiner-epoch-99-avg-1-chunk-16-left-128.onnx',
            num_threads=2, sample_rate=16000, feature_dim=80,
            enable_endpoint_detection=True,
        )

        # Read wav file
        with wave.open(audio_path, 'rb') as wf:
            sample_rate = wf.getframerate()
            channels = wf.getnchannels()
            frames = wf.readframes(wf.getnframes())
            samples = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768.0
            if channels == 2:
                samples = samples[::2]  # Take left channel

        stream = recognizer.create_stream()
        stream.accept_waveform(sample_rate, samples)

        # Process in chunks
        while recognizer.is_ready(stream):
            recognizer.decode_stream(stream)

        result = recognizer.get_result(stream)
        print(f"  Result: {result}")
        return result
    except Exception as e:
        print(f"  ERROR: {e}")
        return None

def test_sherpa_mic(duration=5):
    """Test sherpa-onnx with microphone - real-time streaming using pasimple"""
    print(f"\n[SHERPA-ONNX] Recording {duration}s from mic (streaming)...")
    try:
        import sherpa_onnx
        from pasimple import PaSimple, PA_STREAM_RECORD, PA_SAMPLE_S16LE
        import numpy as np

        model_dir = os.path.expanduser('~/.local/share/sherpa-onnx/sherpa-onnx-streaming-zipformer-en-2023-06-26')
        if not os.path.exists(model_dir):
            print("  ERROR: Model not found. Run: aio voice (to auto-download)")
            return None

        recognizer = sherpa_onnx.OnlineRecognizer.from_transducer(
            tokens=f'{model_dir}/tokens.txt',
            encoder=f'{model_dir}/encoder-epoch-99-avg-1-chunk-16-left-128.onnx',
            decoder=f'{model_dir}/decoder-epoch-99-avg-1-chunk-16-left-128.onnx',
            joiner=f'{model_dir}/joiner-epoch-99-avg-1-chunk-16-left-128.onnx',
            num_threads=2, sample_rate=16000, feature_dim=80,
            enable_endpoint_detection=True, rule1_min_trailing_silence=2.0, rule2_min_trailing_silence=1.0,
        )

        stream = recognizer.create_stream()
        sample_rate = 48000  # Use native rate, sherpa resamples internally
        chunk_samples = int(0.1 * sample_rate)  # 100ms chunks
        chunk_bytes = chunk_samples * 2  # 2 bytes per int16 sample

        print(f"  Sample rate: {sample_rate}Hz (pasimple)")
        print("  Speak now! (real-time partial results shown)")

        all_results = []
        last_result = ''

        with PaSimple(PA_STREAM_RECORD, PA_SAMPLE_S16LE, 1, sample_rate) as pa:
            end = time.time() + duration
            while time.time() < end:
                data = pa.read(chunk_bytes)
                samples = np.frombuffer(data, dtype=np.int16).astype(np.float32) / 32768.0
                stream.accept_waveform(sample_rate, samples)

                while recognizer.is_ready(stream):
                    recognizer.decode_stream(stream)

                result = recognizer.get_result(stream)
                if result and result != last_result:
                    print(f"\r  Partial: {result}", end='', flush=True)
                    last_result = result

                if recognizer.is_endpoint(stream) and result:
                    print(f"\n  [ENDPOINT]: {result}")
                    all_results.append(result)
                    recognizer.reset(stream)
                    last_result = ''

        # Get final result
        final = recognizer.get_result(stream)
        if final:
            all_results.append(final)

        full_text = ' '.join(all_results)
        print(f"\n  Final: {full_text}")
        return full_text
    except Exception as e:
        print(f"  ERROR: {e}")
        import traceback; traceback.print_exc()
        return None

# ============== METHOD 2: WHISPER (batch) ==============
def test_whisper_file(audio_path, model_size='base'):
    """Test OpenAI Whisper with audio file"""
    print(f"\n[WHISPER {model_size}] Testing with file...")
    try:
        import whisper

        print("  Loading model...")
        model = whisper.load_model(model_size)

        print("  Transcribing...")
        start = time.time()
        result = model.transcribe(audio_path)
        elapsed = time.time() - start

        text = result['text'].strip()
        print(f"  Result ({elapsed:.2f}s): {text}")
        return text
    except Exception as e:
        print(f"  ERROR: {e}")
        return None

def test_whisper_mic(duration=5, model_size='base'):
    """Test Whisper with microphone recording"""
    print(f"\n[WHISPER {model_size}] Recording {duration}s from mic...")
    try:
        from pasimple import record_wav
        import whisper
        import tempfile

        f = tempfile.mktemp(suffix='.wav')
        print("  Recording... (speak now)")
        record_wav(f, duration)
        print("  Loading model...")
        model = whisper.load_model(model_size)
        print("  Transcribing...")
        start = time.time()
        result = model.transcribe(f)
        elapsed = time.time() - start
        os.unlink(f)

        text = result['text'].strip()
        print(f"  Result ({elapsed:.2f}s): {text}")
        return text
    except Exception as e:
        print(f"  ERROR: {e}")
        return None

# ============== METHOD 3: VOSK (streaming) ==============
def test_vosk_file(audio_path):
    """Test Vosk with audio file"""
    print("\n[VOSK] Testing with file...")
    try:
        import vosk
        import wave
        import json

        vosk.SetLogLevel(-1)
        model = vosk.Model(lang="en-us")

        with wave.open(audio_path, 'rb') as wf:
            rec = vosk.KaldiRecognizer(model, wf.getframerate())
            rec.SetWords(True)

            results = []
            while True:
                data = wf.readframes(4000)
                if len(data) == 0:
                    break
                if rec.AcceptWaveform(data):
                    r = json.loads(rec.Result())
                    if r.get('text'):
                        results.append(r['text'])

            # Final result
            r = json.loads(rec.FinalResult())
            if r.get('text'):
                results.append(r['text'])

        text = ' '.join(results)
        print(f"  Result: {text}")
        return text
    except Exception as e:
        print(f"  ERROR: {e}")
        return None

def test_vosk_mic(duration=5):
    """Test Vosk with microphone - real-time streaming using pasimple"""
    print(f"\n[VOSK] Recording {duration}s from mic (streaming)...")
    try:
        import vosk
        from pasimple import PaSimple, PA_STREAM_RECORD, PA_SAMPLE_S16LE
        import json

        vosk.SetLogLevel(-1)
        model = vosk.Model(lang="en-us")

        sample_rate = 48000  # Use native rate
        rec = vosk.KaldiRecognizer(model, sample_rate)
        rec.SetWords(True)

        chunk_samples = int(0.1 * sample_rate)  # 100ms
        chunk_bytes = chunk_samples * 2

        print(f"  Sample rate: {sample_rate}Hz (pasimple)")
        print("  Speak now! (real-time partial results shown)")

        all_results = []

        with PaSimple(PA_STREAM_RECORD, PA_SAMPLE_S16LE, 1, sample_rate) as pa:
            end = time.time() + duration
            while time.time() < end:
                data = pa.read(chunk_bytes)
                if rec.AcceptWaveform(data):
                    r = json.loads(rec.Result())
                    if r.get('text'):
                        print(f"\n  [FINAL]: {r['text']}")
                        all_results.append(r['text'])
                else:
                    p = json.loads(rec.PartialResult()).get('partial', '')
                    if p:
                        print(f"\r  Partial: {p}", end='', flush=True)

        # Final result
        r = json.loads(rec.FinalResult())
        if r.get('text'):
            all_results.append(r['text'])

        text = ' '.join(all_results)
        print(f"\n  Final: {text}")
        return text
    except Exception as e:
        print(f"  ERROR: {e}")
        import traceback; traceback.print_exc()
        return None

# ============== MAIN ==============
def main():
    if len(sys.argv) < 2:
        print(__doc__)
        print("\nAvailable methods: sherpa, whisper, vosk")
        print("\nExamples:")
        print("  python test_voice.py file /tmp/test_audio.wav")
        print("  python test_voice.py file /tmp/test_audio.wav whisper")
        print("  python test_voice.py mic 5")
        print("  python test_voice.py mic 10 sherpa")
        print("  python test_voice.py all")  # Run all methods
        sys.exit(1)

    mode = sys.argv[1]

    if mode == 'file':
        audio_path = sys.argv[2] if len(sys.argv) > 2 else '/tmp/test_audio.wav'
        method = sys.argv[3] if len(sys.argv) > 3 else 'all'

        if not os.path.exists(audio_path):
            print(f"ERROR: File not found: {audio_path}")
            sys.exit(1)

        print(f"Testing with file: {audio_path}")

        if method in ('all', 'sherpa'):
            test_sherpa_file(audio_path)
        if method in ('all', 'whisper'):
            test_whisper_file(audio_path)
        if method in ('all', 'vosk'):
            test_vosk_file(audio_path)

    elif mode == 'mic':
        duration = int(sys.argv[2]) if len(sys.argv) > 2 else 5
        method = sys.argv[3] if len(sys.argv) > 3 else 'sherpa'

        print(f"Testing with microphone ({duration}s)")

        if method in ('all', 'sherpa'):
            test_sherpa_mic(duration)
        if method in ('all', 'whisper'):
            test_whisper_mic(duration)
        if method in ('all', 'vosk'):
            test_vosk_mic(duration)

    elif mode == 'all':
        # Test only real-time streaming methods
        duration = int(sys.argv[2]) if len(sys.argv) > 2 else 5
        print(f"=== REAL-TIME STREAMING TESTS ({duration}s each) ===")
        print("Testing SHERPA-ONNX...")
        test_sherpa_mic(duration)
        print("\nTesting VOSK...")
        test_vosk_mic(duration)

    else:
        print(f"Unknown mode: {mode}")
        print("Use: file, mic, or all")

if __name__ == '__main__':
    main()
