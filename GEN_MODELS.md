# Agent-Driven Model Generation System

## Overview

`quantumstd1/gen_models.py` is a two-stage orchestrator that uses Claude sub-agents to autonomously generate, validate, and deploy sklearn model files for the quantumstd1 stock prediction system. Each generated model fits within the existing 1s time / 1GB RAM constraint system and integrates directly with CFA fusion.

## Architecture

```
                    gen_models.py (orchestrator)
                           |
              Stage 1: claude -p (planner)
              "List N diverse sklearn classifiers"
              (includes hyperparameter variants)
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
              PASS → save        FAIL → retry with error
                                 (up to 2 retries)
                           |
                    Batch loop until target reached
```

## Usage

```sh
cd ~/projects/quantumstd1

# Default: generate 100 models, 4 parallel workers, 15 per batch
python3 gen_models.py

# Custom target, parallelism, batch size
python3 gen_models.py --target 50 --workers 6 --batch 20
```

## Batch Loop

The orchestrator runs in a loop until the target model count is reached. Each batch:
1. Reloads existing model names (auto-dedup across batches)
2. Reloads example files (capped at 8 to keep prompts manageable)
3. Plans a batch of models via stage 1
4. Generates + validates in parallel via stage 2
5. Stops if a batch produces zero successes (avoids infinite loop)

## Two-Stage Process

### Stage 1: Planning (1 claude call per batch)

The planner agent receives:
- Count of models to generate
- List of ALL existing model filenames (auto-dedup)
- Instruction to include both different estimators AND hyperparameter variants
- Format: `ClassName: EstimatorClass(params)`
- Constraints: fast (<1s on 30k samples), diverse, available in sklearn

Example output:
```
RfShallow: RandomForestClassifier(n_estimators=50, max_depth=5)
SVCPoly: SVC(kernel='poly', degree=3, probability=True)
BaggingSVM: BaggingClassifier(estimator=SVC(kernel='linear'), n_estimators=10)
```

**Parsing:** Strips numbering, backticks, filters lines by classifier keywords.

### Stage 2: Generation + Validation (N parallel claude calls)

Each sub-agent receives:
1. **Sample of existing model source** (`load_examples()` reads up to 8 `models/*.py` — first 4 + last 4)
2. **The contract:** inherit `SklearnTrainer`, wrap one sklearn estimator, follow exact pattern
3. **Specific instruction:** which estimator to use with what params
4. **Error context** (on retry): the validation error from the previous attempt

## Sub-Agent Prompt Structure

```
build_prompt(idea, examples, error=None)
```

The prompt contains:
- Contract description (constraints, base class role)
- Sample existing model source code (real working examples, not a template)
- Rules (exact pattern, unique class name, no duplicates, no markdown)
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

**Init bypass:** During validation, `models/__init__.py` is temporarily emptied (thread-safe via lock) to avoid importing quantum_lstm (PennyLane) and classical_alstm (TensorFlow) which may not be installed. Restored in `finally` block.

### Retry on Failure
If validation fails, the broken file is deleted and the sub-agent is re-prompted with the error message appended. Up to 2 retries (3 total attempts).

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

### Full 100-model run (7 batches, ~15 minutes)

| Batch | Planned | Created | Failed | Failure types |
|-------|---------|---------|--------|---------------|
| 1     | 15      | 15      | 0      | - |
| 2     | 16      | 14      | 2      | 1 timeout, 1 bad output |
| 3     | 16      | 15      | 1      | 1 timeout |
| 4     | 16      | 15      | 1      | 1 timeout |
| 5     | 16      | 14      | 2      | 1 timeout, 1 missing decision_function |
| 6     | 16      | 14      | 2      | 1 timeout, 1 bad output |
| 7     | 16      | 15      | 1      | 1 timeout |
| **Total** | **111** | **102** | **9** | **92% success rate** |

**131 total models** in directory (including infrastructure files).

### Model diversity

Generated models span all sklearn classifier families:
- **Ensembles:** RandomForest, ExtraTrees, GradientBoosting, HistGradientBoosting, AdaBoost, Bagging, Voting, Stacking
- **Linear:** LogisticRegression, SGD, Perceptron, PassiveAggressive, RidgeClassifier
- **SVM:** SVC (rbf/poly/sigmoid), NuSVC, LinearSVC
- **Neighbors:** KNeighbors (distance/uniform/manhattan/cosine)
- **Naive Bayes:** Gaussian, Bernoulli, Complement, Multinomial
- **Trees:** DecisionTree (various depths/criteria)
- **Other:** LDA, QDA, GaussianProcess, NearestCentroid, LabelSpreading, LabelPropagation
- **Wrappers:** CalibratedClassifierCV, Pipeline (BernoulliRBM+LR)
- **Hyperparameter variants:** rf_shallow, rf_deep, rf_balanced, mlp_small, mlp_deep, mlp_wide, etc.

## Key Implementation Details

| Component | Detail |
|-----------|--------|
| Sub-agent call | `claude -p` (print mode, exits after response) |
| Parallelism | `ThreadPoolExecutor(max_workers=4)` |
| Timeout per agent | 90s generation, 30s validation |
| Context per agent | 8 example model files (first 4 + last 4, ~2KB each) |
| Dedup | `existing_names()` refreshed each batch + file existence at write |
| Filename convention | CamelCase class → snake_case file (`AdaBoostTrainer` → `ada_boost.py`) |
| Error feedback | Validation stderr (last 500 chars) appended to retry prompt |
| Init safety | Thread-locked `__init__.py` swap during validation |
| Batch loop | Runs until target, stops on zero-success batch |
