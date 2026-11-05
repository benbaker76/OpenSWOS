#!/usr/bin/env python3
"""career_golden_diff.py -- what differs between two JSON career saves.

    python3 -I scripts/career_golden_diff.py A.CAR B.CAR [N]

For the golden career test (scripts/career_golden_test.sh): when a season's save parts from
the record, compare the two saves field by field. Prints the number of differing values and
the first N (default 40) as path: A -> B, then a count per top-level field.
"""
import collections
import json
import sys


def diff(x, y, path, out):
    if type(x) is not type(y):
        out.append((path, x, y))
    elif isinstance(x, dict):
        for k in sorted(set(x) | set(y)):
            if k not in x or k not in y:
                out.append((f'{path}/{k}', x.get(k), y.get(k)))
            else:
                diff(x[k], y[k], f'{path}/{k}', out)
    elif isinstance(x, list):
        if len(x) != len(y):
            out.append((f'{path}[len]', len(x), len(y)))
        for i, (p, q) in enumerate(zip(x, y)):
            diff(p, q, f'{path}[{i}]', out)
    elif x != y:
        out.append((path, x, y))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    a = json.load(open(sys.argv[1], encoding='utf-8'))
    b = json.load(open(sys.argv[2], encoding='utf-8'))
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    out = []
    diff(a, b, '', out)
    print(f'{len(out)} differing values')
    for path, x, y in out[:n]:
        print(f'  {path}: {repr(x)[:60]} -> {repr(y)[:60]}')
    if out:
        top = collections.Counter(p.split('/')[1].split('[')[0] for p, _, _ in out)
        print('by field:', ', '.join(f'{k} {v}' for k, v in top.most_common()))
    return 1 if out else 0


if __name__ == '__main__':
    sys.exit(main())
