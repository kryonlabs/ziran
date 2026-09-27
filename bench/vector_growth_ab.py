#!/usr/bin/env python3
"""Validated, shuffled whole-process comparison of two vector-growth binaries."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(path, size, expected):
    start = time.perf_counter_ns()
    result = subprocess.run([str(path), str(size)], capture_output=True,
                            text=True, check=False)
    elapsed = (time.perf_counter_ns() - start) / 1_000_000
    if result.returncode or result.stdout.strip() != expected:
        raise RuntimeError(f'{path}: exit={result.returncode}, '
                           f'output={result.stdout!r}, error={result.stderr!r}; '
                           f'expected {expected}')
    return elapsed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--size', type=int, default=2_000_000)
    parser.add_argument('--repetitions', type=int, default=20)
    parser.add_argument('--seed', type=int, default=29)
    args = parser.parse_args()
    if args.size < 1 or args.repetitions < 1:
        parser.error('size and repetitions must be positive')
    if args.out.exists():
        parser.error(f'output already exists: {args.out}')
    paths = {'before': args.before.resolve(), 'after': args.after.resolve()}
    for path in paths.values():
        if not path.is_file() or not os.access(path, os.X_OK):
            parser.error(f'not an executable file: {path}')
    started_utc = datetime.now(timezone.utc).isoformat()
    expected = str(37 * args.size * (args.size - 1) // 2 + 11 * args.size)
    for path in paths.values():
        for _ in range(2):
            run(path, args.size, expected)
    randomizer = random.Random(args.seed)
    samples = {'before': [], 'after': []}
    for round_number in range(args.repetitions):
        order = list(paths)
        randomizer.shuffle(order)
        for name in order:
            samples[name].append({'round': round_number,
                                  'elapsed_ms': run(paths[name], args.size,
                                                    expected)})
    medians = {name: statistics.median(sample['elapsed_ms'] for sample in values)
               for name, values in samples.items()}
    result = {
        'schema_version': 1,
        'started_utc': started_utc,
        'workload': 'Vec(s32) growth and scan, fresh process per sample',
        'size': args.size,
        'expected_checksum': expected,
        'warmups_per_binary': 2,
        'repetitions_per_binary': args.repetitions,
        'seed': args.seed,
        'machine': {'platform': platform.platform(), 'machine': platform.machine(),
                    'processor': platform.processor(), 'cpu_count': os.cpu_count()},
        'binaries': {name: {'path': str(path), 'sha256': digest(path)}
                     for name, path in paths.items()},
        'script_sha256': digest(Path(__file__)),
        'samples_ms': samples,
        'medians_ms': medians,
        'ratio_after_before': medians['after'] / medians['before'],
        'limitations': ['No CPU isolation or frequency control.',
                        'Fresh-process times include startup and printing.',
                        'These binaries are caller-supplied; their provenance '
                        'must be recorded alongside the result.'],
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(f'{medians["before"]:.3f} ms -> {medians["after"]:.3f} ms '
          f'({result["ratio_after_before"]:.3f}x); {args.out}')


if __name__ == '__main__':
    main()
