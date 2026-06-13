# Gemma 4 Thinking: How It Works

Thinking is trained behavior, not a system prompt. The model generates `<think>...</think>` tokens before answering because training data contained them. Same weights serve both — no separate thinking module. `layer_output_scale` is task-agnostic: pruning degrades thinking and answering equally.

Ollama's `PARSER gemma4` routes `<think>` tokens to a separate JSON field. `"think":false` suppresses the token at logit level. `"think":"low"/"medium"/"high"` controls budget. No system prompt involved — template is just `<start_of_turn>model\n`.

Key finding: E2B (2.3B) scored 100% on advanced reasoning WITH thinking. E4B (4.5B) scored 80%. Thinking is the equalizer — token budget matters more than param count. The model that thinks well beats the bigger model that thinks poorly.

For compression: preserve thinking quality above all. Test with `think=false` for fast probes during pruning, `think=true` for final validation only.
