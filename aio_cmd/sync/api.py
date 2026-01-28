"""Central sync API - dispatches to backends, checks consensus"""
from .backends import BACKENDS

def _run(op, *a):
    r = {b.__name__.split('.')[-1]: getattr(b, op)(*a) for b in BACKENDS}
    v = list(r.values())
    len(set(str(x) for x in v)) > 1 and print(f"⚠ CONFLICT: {r}")
    return v[0] if v else None

put = lambda t, k, d: _run('put', t, k, d)
get = lambda t, k: _run('get', t, k)
delete = lambda t, k: _run('delete', t, k)
list_all = lambda t: _run('list_all', t)
sync = lambda: _run('sync')
