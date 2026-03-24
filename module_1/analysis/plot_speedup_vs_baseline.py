#!/usr/bin/env python3
"""
Genera il grafico Speedup vs Baseline al variare di N.
Stile pulito: sfondo bianco, linea tratteggia rossa a S=1.0, log-x.
"""

import csv
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from collections import defaultdict
import os

def main():
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    csv_path = os.path.join(base_dir, 'results', 'cpu_results.csv')
    out_dir  = os.path.join(base_dir, 'results', 'plots')
    os.makedirs(out_dir, exist_ok=True)

    # Leggi solo l'esperimento "Sweep N"
    rows = []
    with open(csv_path) as f:
        for r in csv.DictReader(f):
            if 'Sweep N' in r['experiment']:
                rows.append(r)

    # Raggruppa per (N, impl)
    data = defaultdict(dict)
    for r in rows:
        N = int(r['N'])
        impl = r['impl']
        data[N][impl] = float(r['median_ms'])

    Ns = sorted(data.keys())

    # Calcola speedup = t_baseline / t_impl
    autovec_speedup = []
    avx2_speedup = []
    autovec_Ns = []
    avx2_Ns = []

    for N in Ns:
        d = data[N]
        if 'baseline' not in d:
            continue
        t_base = d['baseline']
        if 'autovec' in d:
            autovec_Ns.append(N)
            autovec_speedup.append(t_base / d['autovec'])
        if 'avx2' in d:
            avx2_Ns.append(N)
            avx2_speedup.append(t_base / d['avx2'])

    # --- Plot ---
    fig, ax = plt.subplots(figsize=(10, 6))

    # Linea baseline a 1.0
    ax.axhline(y=1.0, color='red', linestyle='--', linewidth=1.5,
               label='Baseline', zorder=1)

    # Autovec
    ax.plot(autovec_Ns, autovec_speedup, marker='o', markersize=7,
            linewidth=2, color='#1f77b4', label='auto', zorder=3)

    # AVX2
    ax.plot(avx2_Ns, avx2_speedup, marker='o', markersize=7,
            linewidth=2, color='#ff7f0e', label='avx2', zorder=3)

    ax.set_xscale('log')
    ax.set_xlabel('Input Size (N)', fontsize=12)
    ax.set_ylabel('Speedup Factor', fontsize=12)
    ax.set_title('Speedup vs Baseline', fontsize=14)

    # Formatter: mostra N come a·10^e  (es. 10^6, 5·10^7, 2·10^8)
    import math
    def fmt_N(x, _):
        if x <= 0:
            return '0'
        exp = int(math.floor(math.log10(x)))
        coeff = x / 10**exp
        if abs(coeff - 1.0) < 0.01:
            return f'$10^{{{exp}}}$'
        return f'${coeff:.0f}' + r'\cdot' + f'10^{{{exp}}}$'

    ax.set_xticks(Ns)
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(fmt_N))
    ax.xaxis.set_minor_formatter(ticker.NullFormatter())
    ax.grid(True, alpha=0.3, linewidth=0.5)
    ax.legend(fontsize=11)

    # Tick minori per chiarezza
    ax.tick_params(axis='both', which='major', labelsize=10)

    fig.tight_layout()
    out_path = os.path.join(out_dir, '16_speedup_vs_baseline.png')
    fig.savefig(out_path, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close(fig)
    print(f'  {out_path}')


if __name__ == '__main__':
    main()
