#!/usr/bin/env python3
"""
parse_results.py — Converte l'output testuale dei benchmark in CSV strutturato.

Legge i file .txt/.out prodotti da run_bench.sh e run_cuda.sh, estrae i campi
numerici con regex, e produce due CSV:
  - results/cpu_results.csv  (baseline, autovec, avx2)
  - results/cuda_results.csv (cuda con breakdown H2D/kernel/D2H)

Uso:
  python3 analysis/parse_results.py                          # cerca in results/
  python3 analysis/parse_results.py results/bench_cpu.txt    # file specifico
"""

import re
import csv
import sys
import os
from pathlib import Path

# ---- regex per catturare i campi dall'output dei binari ----

# Riga tipo:
# plain (no-vec)            N=1000000     median=0.275    ms  stddev=0.048  ms  throughput=3640.2 Mkeys/s
RE_CPU_RESULT = re.compile(
    r'(?P<label>[\w\s()/-]+?)\s+'
    r'N=(?P<N>\d+)\s+'
    r'median=(?P<median>[\d.]+)\s+ms\s+'
    r'stddev=(?P<stddev>[\d.]+)\s+ms\s+'
    r'throughput=(?P<throughput>[\d.]+)\s+Mkeys/s'
)

# Riga tipo:
# Distribuzione: min=3764 max=4070 atteso=3906.2 max/atteso=1.0419
RE_DISTRIB = re.compile(
    r'Distribuzione:\s+min=(?P<min>\d+)\s+max=(?P<max>\d+)\s+'
    r'atteso=(?P<expected>[\d.]+)\s+max/atteso=(?P<ratio>[\d.]+)'
)

# Riga tipo:
# Speedup AVX2 vs scalar: 2.35x
RE_SPEEDUP = re.compile(
    r'Speedup\s+.*?:\s+(?P<speedup>[\d.]+)x'
)

# Riga contesto:
# --- N=100000000 P=256 ---
# --- N=100000000 P=256 key_space=1000 ---
RE_CONTEXT = re.compile(
    r'---\s+N=(?P<N>\d+)\s+P=(?P<P>\d+)'
    r'(?:\s+key_space=(?P<key_space>\d+))?\s*---'
)

# Riga tag implementazione:
# [baseline]  [autovec]  [avx2]
RE_TAG = re.compile(r'^\[(\w+)\]$')

# Riga esperimento:
# === Experiment 1: Sweep N (P=256) ===
RE_EXPERIMENT = re.compile(r'===\s+(.+?)\s+===')

# Checksum
RE_CHECKSUM = re.compile(r'checksum=0x([0-9a-fA-F]+)')

# P e shift dalla riga di dettaglio
RE_PARAMS = re.compile(r'P=(?P<P>\d+)\s+shift=(?P<shift>\d+)')

# ---- CUDA specifico ----

# H->D  : 32.456 ms
RE_CUDA_PHASE = re.compile(
    r'(?P<phase>H->D|Kernel|D->H|Totale)\s*:\s*(?P<time>[\d.]+)\s*ms'
)

# Throughput (solo kernel): 12345.6 Mkeys/s
RE_CUDA_TPUT = re.compile(
    r'Throughput\s+\((?P<scope>[^)]+)\):\s+(?P<tput>[\d.]+)\s+Mkeys/s'
)


def parse_cpu_file(filepath):
    """Parsa un file di risultati CPU e restituisce lista di dizionari."""
    rows = []
    current_experiment = ""
    current_N = 0
    current_P = 0
    current_ks = 0
    current_tag = ""
    pending_row = None

    with open(filepath) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue

            m = RE_EXPERIMENT.search(line)
            if m:
                current_experiment = m.group(1)
                continue

            m = RE_CONTEXT.match(line)
            if m:
                current_N = int(m.group('N'))
                current_P = int(m.group('P'))
                current_ks = int(m.group('key_space') or 0)
                continue

            m = RE_TAG.match(line)
            if m:
                # salva riga precedente se pendente
                if pending_row:
                    rows.append(pending_row)
                current_tag = m.group(1)
                pending_row = None
                continue

            m = RE_CPU_RESULT.search(line)
            if m:
                pending_row = {
                    'experiment': current_experiment,
                    'impl': current_tag or m.group('label').strip(),
                    'N': int(m.group('N')),
                    'P': current_P,
                    'key_space': current_ks,
                    'median_ms': float(m.group('median')),
                    'stddev_ms': float(m.group('stddev')),
                    'throughput_Mkeys_s': float(m.group('throughput')),
                    'dist_min': '',
                    'dist_max': '',
                    'dist_ratio': '',
                    'checksum': '',
                }
                continue

            if pending_row:
                m = RE_DISTRIB.search(line)
                if m:
                    pending_row['dist_min'] = int(m.group('min'))
                    pending_row['dist_max'] = int(m.group('max'))
                    pending_row['dist_ratio'] = float(m.group('ratio'))
                    continue

                m = RE_CHECKSUM.search(line)
                if m:
                    pending_row['checksum'] = m.group(1)
                    continue

    if pending_row:
        rows.append(pending_row)

    return rows


def parse_cuda_file(filepath):
    """Parsa un file di risultati CUDA e restituisce lista di dizionari."""
    rows = []
    current_experiment = ""
    current_N = 0
    current_P = 0

    pending = {}

    with open(filepath) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue

            m = RE_EXPERIMENT.search(line)
            if m:
                current_experiment = m.group(1)
                continue

            m = RE_CONTEXT.match(line)
            if m:
                # salva riga precedente
                if pending.get('N'):
                    rows.append(pending)
                current_N = int(m.group('N'))
                current_P = int(m.group('P'))
                pending = {
                    'experiment': current_experiment,
                    'N': current_N,
                    'P': current_P,
                    'h2d_ms': '', 'kernel_ms': '', 'd2h_ms': '', 'total_ms': '',
                    'tput_kernel_Mkeys_s': '', 'tput_e2e_Mkeys_s': '',
                    'checksum': '',
                }
                continue

            m = RE_CUDA_PHASE.search(line)
            if m:
                phase = m.group('phase')
                t = float(m.group('time'))
                if phase == 'H->D':     pending['h2d_ms'] = t
                elif phase == 'Kernel': pending['kernel_ms'] = t
                elif phase == 'D->H':   pending['d2h_ms'] = t
                elif phase == 'Totale': pending['total_ms'] = t
                continue

            m = RE_CUDA_TPUT.search(line)
            if m:
                scope = m.group('scope')
                tput = float(m.group('tput'))
                if 'kernel' in scope.lower():
                    pending['tput_kernel_Mkeys_s'] = tput
                else:
                    pending['tput_e2e_Mkeys_s'] = tput
                continue

            m = RE_CHECKSUM.search(line)
            if m and pending:
                pending['checksum'] = m.group(1)

    if pending.get('N'):
        rows.append(pending)

    return rows


def write_csv(rows, outpath, fieldnames):
    """Scrive una lista di dizionari in CSV."""
    os.makedirs(os.path.dirname(outpath), exist_ok=True)
    with open(outpath, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)
    print(f"  scritto {outpath}  ({len(rows)} righe)")


def main():
    results_dir = Path('results')
    outdir = results_dir

    # trova i file di input
    cpu_files = []
    cuda_files = []

    if len(sys.argv) > 1:
        # file passati esplicitamente
        for p in sys.argv[1:]:
            if 'cuda' in p.lower():
                cuda_files.append(p)
            else:
                cpu_files.append(p)
    else:
        # cerca automaticamente in results/
        if results_dir.exists():
            for f in sorted(results_dir.iterdir()):
                name = f.name.lower()
                if not (name.endswith('.txt') or name.endswith('.out')):
                    continue
                if 'cuda' in name:
                    cuda_files.append(str(f))
                elif 'bench' in name or 'cpu' in name:
                    cpu_files.append(str(f))

    if not cpu_files and not cuda_files:
        print("Nessun file di risultati trovato.")
        print("Uso: python3 analysis/parse_results.py [file1.txt file2.txt ...]")
        print("  oppure esegui dopo aver salvato i risultati in results/")
        sys.exit(1)

    # CPU
    if cpu_files:
        all_cpu = []
        for f in cpu_files:
            print(f"Parsing CPU: {f}")
            all_cpu.extend(parse_cpu_file(f))

        if all_cpu:
            cpu_fields = [
                'experiment', 'impl', 'N', 'P', 'key_space',
                'median_ms', 'stddev_ms', 'throughput_Mkeys_s',
                'dist_min', 'dist_max', 'dist_ratio', 'checksum'
            ]
            write_csv(all_cpu, str(outdir / 'cpu_results.csv'), cpu_fields)
        else:
            print("  nessun risultato CPU trovato nel file")

    # CUDA
    if cuda_files:
        all_cuda = []
        for f in cuda_files:
            print(f"Parsing CUDA: {f}")
            all_cuda.extend(parse_cuda_file(f))

        if all_cuda:
            cuda_fields = [
                'experiment', 'N', 'P',
                'h2d_ms', 'kernel_ms', 'd2h_ms', 'total_ms',
                'tput_kernel_Mkeys_s', 'tput_e2e_Mkeys_s', 'checksum'
            ]
            write_csv(all_cuda, str(outdir / 'cuda_results.csv'), cuda_fields)
        else:
            print("  nessun risultato CUDA trovato nel file")


if __name__ == '__main__':
    main()
