# Gemma 4 Local Model Benchmark Report

**Date:** 2026-04-04
**Hardware:** Ubuntu — RTX 4080 Laptop 12GB VRAM, 30GB RAM
**Ollama:** v0.20.2
**Tool:** `bench_llm.py` — two-tier quality benchmark with parallel execution

## Results

| Model | Params | Tier 1 (no think) | Tier 2 (thinking) | T1 time | T2 time | T2 tok/s |
|-------|--------|-------------------|-------------------|---------|---------|----------|
| E2B | 2.3B eff | 18/20 (90%) | **10/10 (100%)** | 11.6s | 58s | 68 |
| E4B | 4.5B eff | 19/20 (95%) | 8/10 (80%) | 21.3s | 103s | 30 |

## Key Finding

**E2B outperformed E4B on advanced reasoning tasks with thinking enabled.**

E4B failures:
- `y=x^2-3x+2` at x=7: E4B answered 140 (correct: 30)
- `map auto_home 0Bi 0Bi 0Bi 100%`: E4B said "100% full" (correct: automount placeholder)

E2B got both right. E2B was also 2x faster (58s vs 103s).

## Method

### Tier 1: Fast Probes (no thinking, parallel=4)
20 questions testing floor capabilities — what survives any compression.
- Math basics (2+2, 7*8, sqrt)
- Knowledge (capitals, elements, dates)
- Code output prediction (print, len, slice)
- Logic (prime check, boolean)
- Protocol adherence (CMD: prefix)
- Format following (JSON, CSV, completion)

Thinking disabled via `"think": false` in ollama API. 4 concurrent requests.
Target: <2s per question after model warm-up.

### Tier 2: Advanced Capability (thinking enabled, parallel=2)
10 questions testing ceiling — what dies first when pruning.
- Multi-step math (347*28, polynomial eval, prime sums)
- Code generation (palindrome check, fibonacci)
- System understanding (df -h autofs interpretation)
- Reasoning (box swap puzzle)
- Complex commands (find largest files in bash)
- Pattern recognition (sequence completion)
- Multi-step word problems (distance = rate * time)

Thinking enabled via `"think": true`, max 1024 tokens. 2 concurrent requests.
Automated checking via exact match, contains, regex, and lambda validators.

### How Thinking Works

Gemma 4 generates `<think>...</think>` tokens natively — trained behavior, not a system prompt.
Ollama's `PARSER gemma4` routes thinking tokens to a separate JSON field.
- `"think": false` → model skips `<think>` generation entirely, answers immediately
- `"think": true` → model thinks 200-800 tokens before answering
- `"think": "low"/"medium"/"high"` → controls thinking effort (untested)

The thinking overhead is 1-4 seconds per question at 182 tok/s (E2B on RTX 4080).

## Hardware Benchmarks

| Model | Size | Gen tok/s | Prompt tok/s | VRAM | RAM |
|-------|------|-----------|-------------|------|-----|
| E2B | 7.2GB | 182.5 | 526.4 | 7.8GB | 8.0GB |
| E4B | 9.6GB | 118.6 | 438.3 | 10.3GB | 8.2GB |
| 26B MoE | 17GB | 25.1 | 68.9 | 10.8GB | 16GB |
| 31B Dense | 19GB | 4.8 | 7.9 | 11.3GB | 21GB |

On M1 16GB Mac: E2B runs at 24.5 tok/s, E4B at 27.3 gen / 5.1 prompt (swap pressure).

## Replicate

```bash
# 1. Pull model
ollama pull gemma4:e2b

# 2. Run benchmark (from a/ repo root)
python3 my/bench_llm.py gemma4:e2b --all

# 3. Compare models
python3 my/bench_llm.py gemma4:latest --all   # E4B
python3 my/bench_llm.py gemma4:26b --all      # 26B MoE
python3 my/bench_llm.py gemma4:31b --all      # 31B Dense

# 4. Override ollama host for remote testing
OLLAMA_HOST=http://192.168.1.183:11434 python3 my/bench_llm.py gemma4:e2b --all
```

Results saved to `/tmp/bench_<model>_<timestamp>.json`.

## Implications for Compression

The benchmark establishes baselines for pruning experiments:
- **Tier 1 is the floor.** Any compressed model must score ≥18/20. Below that, basic capability is damaged.
- **Tier 2 is the ceiling.** E2B at 100% with thinking is the target. A pruned 31B that can't beat this has no value over the smaller model.
- **E2B's 100% Tier 2 score means the thinking mechanism is the equalizer.** A smaller model that thinks well can beat a larger model that thinks poorly. Compression should preserve thinking quality above all.

The 31B `layer_output_scale` data (60 layers, scales ranging 0.036 to 0.992) combined with this benchmark enables measured pruning: remove a layer, re-run bench, check if scores hold.
