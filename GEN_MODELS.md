# Agent-Driven Model Generation System

## Overview

`quantumstd1/gen_models.py` is a two-stage orchestrator that uses Claude sub-agents to autonomously generate, validate, and deploy sklearn model files for the quantumstd1 stock prediction system. Each generated model fits within the existing 1s time / 1GB RAM constraint system and integrates directly with CFA fusion.

## Architecture

```
                    gen_models.py (orchestrator)
                           |
              Stage 1: claude -p (planner)
              "List N diverse sklearn classifiers"
                           |
                    [model idea list]
                           |
              Stage 2: ThreadPoolExecutor (4 workers)
                    /    |    |    \
              claude -p  ...  ...  claude -p  (sub-agents)
              "Write AdaBoost"    "Write SVC"
                    |                  |
              [python code]      [python code]
                    |                  |
              validate()         validate()
              syntax + import    syntax + import
              + smoke test       + smoke test
                    |                  |
              models/ada_boost.py  models/svc.py
```

## Usage

```sh
cd ~/projects/quantumstd1

# Default: plan 12 models, 4 parallel workers
python3 gen_models.py

# Custom count and parallelism
python3 gen_models.py --count 20 --workers 6
```

## Two-Stage Process

### Stage 1: Planning (1 claude call)

The planner agent receives:
- Count of models to generate
- List of all existing model filenames (auto-dedup)
- Constraints: fast (<1s on 30k samples), diverse, available in sklearn

It outputs a plain list like:
```
AdaBoostClassifier with n_estimators=50, random_state=42
BernoulliNB with alpha=1.0 (no random_state)
HistGradientBoostingClassifier with max_iter=100, max_depth=5
```

**Parsing:** Strips numbering (`1. `), backticks, filters lines by known classifier keywords.

### Stage 2: Generation + Validation (N parallel claude calls)

Each sub-agent receives:
1. **Full source of every existing model** (`load_examples()` reads all `models/*.py`)
2. **The contract:** inherit `SklearnTrainer`, wrap one sklearn estimator, follow exact pattern
3. **Specific instruction:** which estimator to use with what params
4. **Error context** (on retry): the validation error from the previous attempt

## Sub-Agent Prompt Structure

```
build_prompt(idea, examples, error=None)
```

The prompt contains:
- Contract description (constraints, base class role)
- ALL existing model source code (so the agent sees real working examples, not a template)
- Rules (exact pattern, no duplicates, no markdown)
- The specific model to generate
- If retrying: the previous error message with "Fix the issue."

## Validation Pipeline

Each generated model goes through three checks before acceptance:

### 1. Code Extraction (`extract_code`)
Strips markdown fences and surrounding text from claude output. Extracts from first docstring through end of class definition.

### 2. Syntax Check
```python
python3 -m py_compile models/new_model.py
```

### 3. Smoke Test (subprocess)
```python
# In isolated subprocess:
from models.new_model import *
d = np.random.rand(100,5,3).astype('float32')  # dummy 3D input
g = np.random.rand(100,1).astype('float32')     # dummy labels
t = NewModelTrainer(d, g, d[:20], g[:20], d[:20], g[:20])
vp, tp = t.train()
assert 'acc' in vp  # must return {acc, mcc} dict
```

**Init bypass:** During validation, `models/__init__.py` is temporarily emptied to avoid importing quantum_lstm (PennyLane) and classical_alstm (TensorFlow) which may not be installed. Restored in `finally` block.

### Retry on Failure
If validation fails, the broken file is deleted and the sub-agent is re-prompted with the error message appended. Up to 2 retries (3 total attempts).

```python
def gen_and_validate(idea, examples, retries=2):
    error = None
    for attempt in range(retries + 1):
        prompt = build_prompt(idea, examples, error)  # includes error on retry
        # ... generate, write, validate ...
        if ok: return 'ok', fname, cls, msg
        error = msg
        path.unlink()  # remove broken file
    return 'fail', ...
```

## Model Contract

Every generated model follows this exact 14-line pattern:

```python
"""<Description> classifier for stock prediction."""
from sklearn.<module> import <Estimator>
from .sklearn_base import SklearnTrainer

class <Name>Trainer(SklearnTrainer):
    def __init__(self, tra_pv, tra_gt, val_pv, val_gt, tes_pv, tes_gt,
                 hinge=True, time_budget=0.8):
        super().__init__(
            '<DISPLAY NAME>',
            <Estimator>(<params>),
            tra_pv, tra_gt, val_pv, val_gt, tes_pv, tes_gt,
            hinge=hinge, time_budget=time_budget)
```

The `SklearnTrainer` base class (`sklearn_base.py`) provides:
- **Auto-calibration:** Pilot fit on 50 samples, estimate sec/sample, cap training set to fit time_budget
- **Data reshaping:** 3D `(samples, seq, features)` flattened to 2D `(samples, seq*features)`
- **Label binarization:** Continuous [0,1] targets converted to {0,1} at 0.5 threshold
- **Evaluation:** accuracy + MCC on val/test sets
- **Prediction saving:** Writes to CFA-compatible CSV format
- **Monitoring:** Live RAM/CPU/GPU display via Rich

## Integration with quantumstd1

Generated models are not auto-registered in `pred_lstm.py`. To use them:

1. Add import to `models/__init__.py`:
   ```python
   from .ada_boost import AdaBoostTrainer
   ```

2. Add to the sklearn training loop in `pred_lstm.py` (line ~665):
   ```python
   for name, cls in [('RANDOM FOREST', RandomForestTrainer),
                      ('ADA BOOST', AdaBoostTrainer), ...]:
   ```

3. All models automatically participate in CFA fusion if they produce predictions.

## Results

Two runs produced 22 total models (6 original + 16 generated):

| Run | Planned | Created | Failed | Reason |
|-----|---------|---------|--------|--------|
| 1   | 8       | 7       | 1      | Timeout (60s) |
| 2   | 10      | 9       | 0      | - |

Generated models: ada_boost, bagging, bernoulli_nb, calibrated_ridge, decision_tree, extra_trees, gaussian_nb, hist_gradient_boosting, kneighbors, linear_discriminant, passive_aggressive, perceptron, quadratic_discriminant, ridge_classifier, sgd, svc

## Key Implementation Details

| Component | Detail |
|-----------|--------|
| Sub-agent call | `claude -p` (print mode, exits after response) |
| Parallelism | `ThreadPoolExecutor(max_workers=4)` |
| Timeout per agent | 60s generation, 30s validation |
| Context per agent | Full source of all existing models (~2KB per model) |
| Dedup | `existing_names()` checked at plan stage + file existence at write stage |
| Filename convention | CamelCase class → snake_case file (`AdaBoostTrainer` → `ada_boost.py`) |
| Error feedback | Validation stderr (last 500 chars) appended to retry prompt |
