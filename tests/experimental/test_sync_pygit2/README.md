# pygit2 Sync Test (Experimental)

Faster sync test using pygit2 (no subprocess overhead).

## Speed Comparison

| Version | n=100 | ops/sec |
|---------|-------|---------|
| subprocess | 2.3s | ~44 |
| pygit2 | 0.6s | ~170 |

**~4x faster** than subprocess version.

## Limitations

- Requires `pip install pygit2` (libgit2 dependency)
- Logic not exact match to production sync.py
- Uses `master` branch (production uses `main`)
- No broadcast/SSH notification
- Conflict resolution simplified

## Usage

```bash
cd tests/experimental/test_sync_pygit2
python test_sync.py monte
```

## When to Use

- Rapid iteration during sync logic development
- Quick validation of conflict scenarios
- Performance benchmarking

## Production Code

Use `tests/test_sync/test_sync.py` for production-ready logic that can be copy-pasted to `a_cmd/sync.py`.
