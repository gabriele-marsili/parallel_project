#!/usr/bin/env python3
"""
plot_results.py — Genera tutti i grafici di analisi per il Modulo 1.

Legge i CSV prodotti da parse_results.py e genera grafici in results/plots/.

Grafici prodotti:
  1. Throughput vs N          — scalabilità con la dimensione dell'input
  2. Throughput vs P          — sensibilità al numero di partizioni
  3. Speedup vs N             — speedup autovec e AVX2 rispetto al baseline
  4. Speedup vs P             — speedup al variare delle partizioni
  5. Tempo mediano vs N       — confronto tempi assoluti
  6. Distribuzione partizioni — qualità della hash (max/atteso)
  7. Key space sensitivity    — effetto dei duplicati sul throughput
  8. CUDA breakdown           — stacked bar dei tempi H2D/kernel/D2H
  9. CUDA vs CPU              — confronto throughput GPU vs migliore CPU
 10. Tabella riepilogativa    — riassunto numerico come immagine

Uso:
  python3 analysis/plot_results.py                    # default da results/
  python3 analysis/plot_results.py --cpu cpu.csv      # file specifici
  python3 analysis/plot_results.py --no-cuda          # salta grafici CUDA
  python3 analysis/plot_results.py --format pdf       # output PDF (default: png)
"""

import argparse
import sys
import os
from pathlib import Path

import pandas as pd
import matplotlib
matplotlib.use('Agg')  # backend senza GUI, funziona anche via SSH
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

# ---- stile globale ----
plt.rcParams.update({
    'figure.figsize': (10, 6),
    'figure.dpi': 150,
    'axes.grid': True,
    'grid.alpha': 0.3,
    'axes.axisbelow': True,
    'font.size': 11,
    'axes.titlesize': 13,
    'axes.labelsize': 12,
})

# colori per le implementazioni
COLORS = {
    'baseline': '#2196F3',  # blu
    'autovec':  '#4CAF50',  # verde
    'avx2':     '#FF9800',  # arancione
    'cuda':     '#9C27B0',  # viola
}

MARKERS = {
    'baseline': 'o',
    'autovec':  's',
    'avx2':     'D',
    'cuda':     '^',
}

LABELS = {
    'baseline': 'Baseline (no-vec)',
    'autovec':  'Auto-vectorized',
    'avx2':     'AVX2 intrinsics',
    'cuda':     'CUDA (GPU)',
}


def format_N(n):
    """1000000 -> '1M', 10000000 -> '10M', etc."""
    if n >= 1e9: return f'{n/1e9:.0f}G'
    if n >= 1e6: return f'{n/1e6:.0f}M'
    if n >= 1e3: return f'{n/1e3:.0f}K'
    return str(n)


def save_fig(fig, outdir, name, fmt):
    """Salva la figura e chiude."""
    path = outdir / f'{name}.{fmt}'
    fig.savefig(path, bbox_inches='tight', facecolor='white')
    plt.close(fig)
    print(f'  {path}')


# ============================================================================
# Grafici CPU
# ============================================================================

def plot_throughput_vs_N(df, outdir, fmt):
    """Grafico 1: throughput al variare di N, per ogni implementazione."""
    sweep = df[df['key_space'] == 0].copy()
    if sweep.empty:
        return

    # prendi il P più comune per questo sweep
    main_P = sweep['P'].mode().iloc[0] if not sweep['P'].mode().empty else 256
    sweep = sweep[sweep['P'] == main_P]

    fig, ax = plt.subplots()
    for impl in ['baseline', 'autovec', 'avx2']:
        sub = sweep[sweep['impl'] == impl].sort_values('N')
        if sub.empty:
            continue
        ax.plot(sub['N'], sub['throughput_Mkeys_s'],
                marker=MARKERS[impl], color=COLORS[impl],
                label=LABELS[impl], linewidth=2, markersize=7)

    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('Throughput (Mkeys/s)')
    ax.set_title(f'Throughput vs dimensione input (P={main_P})')
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: format_N(int(x))))
    ax.legend()
    save_fig(fig, outdir, '01_throughput_vs_N', fmt)


def plot_throughput_vs_P(df, outdir, fmt):
    """Grafico 2: throughput al variare di P."""
    sweep = df[(df['key_space'] == 0)].copy()
    if sweep.empty:
        return

    # N più grande disponibile
    main_N = sweep['N'].max()
    sweep = sweep[sweep['N'] == main_N]
    if len(sweep['P'].unique()) < 3:
        return

    fig, ax = plt.subplots()
    for impl in ['baseline', 'autovec', 'avx2']:
        sub = sweep[sweep['impl'] == impl].sort_values('P')
        if sub.empty:
            continue
        ax.plot(sub['P'], sub['throughput_Mkeys_s'],
                marker=MARKERS[impl], color=COLORS[impl],
                label=LABELS[impl], linewidth=2, markersize=7)

    ax.set_xlabel('P (numero di partizioni)')
    ax.set_ylabel('Throughput (Mkeys/s)')
    ax.set_title(f'Throughput vs partizioni (N={format_N(main_N)})')
    ax.set_xscale('log', base=2)
    ax.xaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.legend()
    save_fig(fig, outdir, '02_throughput_vs_P', fmt)


def plot_speedup_vs_N(df, outdir, fmt):
    """Grafico 3: speedup di autovec e AVX2 rispetto al baseline."""
    sweep = df[df['key_space'] == 0].copy()
    if sweep.empty:
        return

    main_P = sweep['P'].mode().iloc[0] if not sweep['P'].mode().empty else 256
    sweep = sweep[sweep['P'] == main_P]

    base = sweep[sweep['impl'] == 'baseline'][['N', 'median_ms']].rename(
        columns={'median_ms': 'base_ms'})
    if base.empty:
        return

    fig, ax = plt.subplots()
    for impl in ['autovec', 'avx2']:
        sub = sweep[sweep['impl'] == impl][['N', 'median_ms']]
        if sub.empty:
            continue
        merged = sub.merge(base, on='N')
        merged['speedup'] = merged['base_ms'] / merged['median_ms']
        merged = merged.sort_values('N')
        ax.plot(merged['N'], merged['speedup'],
                marker=MARKERS[impl], color=COLORS[impl],
                label=LABELS[impl], linewidth=2, markersize=7)

    ax.axhline(y=1.0, color='gray', linestyle='--', alpha=0.5, label='Baseline (1.0×)')
    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('Speedup (vs baseline)')
    ax.set_title(f'Speedup vs dimensione input (P={main_P})')
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: format_N(int(x))))
    ax.legend()
    save_fig(fig, outdir, '03_speedup_vs_N', fmt)


def plot_speedup_vs_P(df, outdir, fmt):
    """Grafico 4: speedup al variare di P."""
    sweep = df[(df['key_space'] == 0)].copy()
    if sweep.empty:
        return

    main_N = sweep['N'].max()
    sweep = sweep[sweep['N'] == main_N]
    if len(sweep['P'].unique()) < 3:
        return

    base = sweep[sweep['impl'] == 'baseline'][['P', 'median_ms']].rename(
        columns={'median_ms': 'base_ms'})
    if base.empty:
        return

    fig, ax = plt.subplots()
    for impl in ['autovec', 'avx2']:
        sub = sweep[sweep['impl'] == impl][['P', 'median_ms']]
        if sub.empty:
            continue
        merged = sub.merge(base, on='P')
        merged['speedup'] = merged['base_ms'] / merged['median_ms']
        merged = merged.sort_values('P')
        ax.plot(merged['P'], merged['speedup'],
                marker=MARKERS[impl], color=COLORS[impl],
                label=LABELS[impl], linewidth=2, markersize=7)

    ax.axhline(y=1.0, color='gray', linestyle='--', alpha=0.5, label='Baseline (1.0×)')
    ax.set_xlabel('P (numero di partizioni)')
    ax.set_ylabel('Speedup (vs baseline)')
    ax.set_title(f'Speedup vs partizioni (N={format_N(main_N)})')
    ax.set_xscale('log', base=2)
    ax.xaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.legend()
    save_fig(fig, outdir, '04_speedup_vs_P', fmt)


def plot_time_vs_N(df, outdir, fmt):
    """Grafico 5: tempo mediano vs N."""
    sweep = df[df['key_space'] == 0].copy()
    if sweep.empty:
        return

    main_P = sweep['P'].mode().iloc[0] if not sweep['P'].mode().empty else 256
    sweep = sweep[sweep['P'] == main_P]

    fig, ax = plt.subplots()
    for impl in ['baseline', 'autovec', 'avx2']:
        sub = sweep[sweep['impl'] == impl].sort_values('N')
        if sub.empty:
            continue
        ax.errorbar(sub['N'], sub['median_ms'], yerr=sub['stddev_ms'],
                    marker=MARKERS[impl], color=COLORS[impl],
                    label=LABELS[impl], linewidth=2, markersize=7,
                    capsize=3)

    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('Tempo mediano (ms)')
    ax.set_title(f'Tempo di esecuzione vs dimensione input (P={main_P})')
    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: format_N(int(x))))
    ax.legend()
    save_fig(fig, outdir, '05_time_vs_N', fmt)


def plot_distribution(df, outdir, fmt):
    """Grafico 6: qualità della distribuzione (max/atteso)."""
    sweep = df[(df['key_space'] == 0) & (df['dist_ratio'] != '')].copy()
    if sweep.empty:
        return

    sweep['dist_ratio'] = pd.to_numeric(sweep['dist_ratio'])
    main_P = sweep['P'].mode().iloc[0] if not sweep['P'].mode().empty else 256
    sweep = sweep[sweep['P'] == main_P]

    fig, ax = plt.subplots()
    for impl in ['baseline', 'autovec', 'avx2']:
        sub = sweep[sweep['impl'] == impl].sort_values('N')
        if sub.empty:
            continue
        ax.plot(sub['N'], sub['dist_ratio'],
                marker=MARKERS[impl], color=COLORS[impl],
                label=LABELS[impl], linewidth=2, markersize=7)

    ax.axhline(y=1.0, color='red', linestyle='--', alpha=0.5, label='Distribuzione perfetta')
    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('max(count) / atteso')
    ax.set_title(f'Qualità distribuzione hash (P={main_P})')
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: format_N(int(x))))
    ax.set_ylim(bottom=0.95)
    ax.legend()
    save_fig(fig, outdir, '06_distribution_quality', fmt)


def plot_keyspace_sensitivity(df, outdir, fmt):
    """Grafico 7: throughput al variare del key_space (tasso di duplicati)."""
    sweep = df[df['key_space'].notna()].copy()
    # servono almeno 3 valori diversi di key_space
    if sweep.empty or len(sweep['key_space'].unique()) < 3:
        return

    fig, ax = plt.subplots()
    for impl in ['baseline', 'avx2']:
        sub = sweep[sweep['impl'] == impl].sort_values('key_space')
        if sub.empty:
            continue
        # key_space=0 va mostrato come "full" sull'asse
        labels = []
        for ks in sub['key_space']:
            if ks == 0:
                labels.append('full\n(2⁶⁴)')
            else:
                labels.append(format_N(int(ks)))

        ax.plot(range(len(sub)), sub['throughput_Mkeys_s'],
                marker=MARKERS[impl], color=COLORS[impl],
                label=LABELS[impl], linewidth=2, markersize=7)
        ax.set_xticks(range(len(sub)))
        ax.set_xticklabels(labels)

    ax.set_xlabel('Key space (universo chiavi)')
    ax.set_ylabel('Throughput (Mkeys/s)')
    N_val = sweep['N'].iloc[0]
    P_val = sweep['P'].iloc[0]
    ax.set_title(f'Sensibilità ai duplicati (N={format_N(int(N_val))}, P={int(P_val)})')
    ax.legend()
    save_fig(fig, outdir, '07_keyspace_sensitivity', fmt)


def plot_summary_table(df, outdir, fmt):
    """Grafico 10: tabella riepilogativa come immagine."""
    # prendi N=100M, P=256 (la configurazione "standard")
    sub = df[(df['key_space'] == 0)].copy()
    if sub.empty:
        return

    # la N più grande con tutte le impl
    n_counts = sub.groupby('N')['impl'].nunique()
    best_N = n_counts[n_counts == n_counts.max()].index.max()
    main_P = sub[sub['N'] == best_N]['P'].mode().iloc[0]
    sub = sub[(sub['N'] == best_N) & (sub['P'] == main_P)]

    # una sola riga per implementazione (mediana se duplicati)
    sub = sub.groupby('impl').agg({
        'median_ms': 'median',
        'stddev_ms': 'median',
        'throughput_Mkeys_s': 'median',
    }).reset_index()

    if sub.empty:
        return

    base_ms = sub[sub['impl'] == 'baseline']['median_ms'].values
    base_ms = base_ms[0] if len(base_ms) > 0 else None

    # ordine fisso: baseline, autovec, avx2
    impl_order = ['baseline', 'autovec', 'avx2']
    sub['_order'] = sub['impl'].map({v: i for i, v in enumerate(impl_order)})
    sub = sub.sort_values('_order').drop(columns='_order')

    table_data = []
    for _, row in sub.iterrows():
        speedup = f"{base_ms / row['median_ms']:.2f}×" if base_ms else "—"
        table_data.append([
            LABELS.get(row['impl'], row['impl']),
            f"{row['median_ms']:.3f}",
            f"{row['stddev_ms']:.3f}",
            f"{row['throughput_Mkeys_s']:.1f}",
            speedup,
        ])

    fig, ax = plt.subplots(figsize=(10, 2 + len(table_data) * 0.5))
    ax.axis('off')
    table = ax.table(
        cellText=table_data,
        colLabels=['Implementazione', 'Mediana (ms)', 'Stddev (ms)',
                   'Throughput (Mkeys/s)', 'Speedup'],
        loc='center',
        cellLoc='center',
    )
    table.auto_set_font_size(False)
    table.set_fontsize(11)
    table.scale(1, 1.6)

    # colora header
    for j in range(5):
        table[0, j].set_facecolor('#37474F')
        table[0, j].set_text_props(color='white', fontweight='bold')

    ax.set_title(f'Riepilogo (N={format_N(int(best_N))}, P={int(main_P)})',
                 fontsize=14, pad=20)
    save_fig(fig, outdir, '10_summary_table', fmt)


# ============================================================================
# Grafici CUDA
# ============================================================================

def plot_cuda_breakdown(cuda_df, outdir, fmt):
    """Grafico 8: stacked bar dei tempi CUDA (H2D / kernel / D2H)."""
    sweep = cuda_df.copy()
    if sweep.empty:
        return

    # sweep N con P fisso
    main_P = sweep['P'].mode().iloc[0] if not sweep['P'].mode().empty else 256
    sweep = sweep[sweep['P'] == main_P].sort_values('N')

    if sweep.empty:
        return

    x_labels = [format_N(int(n)) for n in sweep['N']]
    x = np.arange(len(x_labels))
    width = 0.5

    fig, ax = plt.subplots()
    h2d = sweep['h2d_ms'].astype(float).values
    kern = sweep['kernel_ms'].astype(float).values
    d2h = sweep['d2h_ms'].astype(float).values

    ax.bar(x, h2d, width, label='H→D transfer', color='#EF5350')
    ax.bar(x, kern, width, bottom=h2d, label='Kernel', color='#66BB6A')
    ax.bar(x, d2h, width, bottom=h2d + kern, label='D→H transfer', color='#42A5F5')

    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('Tempo (ms)')
    ax.set_title(f'CUDA: breakdown tempi (P={int(main_P)})')
    ax.set_xticks(x)
    ax.set_xticklabels(x_labels)
    ax.legend()
    save_fig(fig, outdir, '08_cuda_breakdown', fmt)


def plot_cuda_vs_cpu(cpu_df, cuda_df, outdir, fmt):
    """Grafico 9: confronto throughput GPU (end-to-end e solo kernel) vs CPU."""
    cpu = cpu_df[(cpu_df['key_space'] == 0)].copy()
    cuda = cuda_df.copy()

    if cpu.empty or cuda.empty:
        return

    # trova N in comune
    cpu_ns = set(cpu['N'].unique())
    cuda_ns = set(cuda['N'].unique())
    common_ns = sorted(cpu_ns & cuda_ns)
    if not common_ns:
        return

    # il P più comune in entrambi
    main_P_cpu = cpu['P'].mode().iloc[0]
    main_P_cuda = cuda['P'].mode().iloc[0]
    if main_P_cpu != main_P_cuda:
        return  # non paragonabili

    P = main_P_cpu
    cpu = cpu[cpu['P'] == P]
    cuda = cuda[cuda['P'] == P]

    fig, ax = plt.subplots()

    # migliore CPU per ogni N
    best_cpu = cpu.groupby('N')['throughput_Mkeys_s'].max().reset_index()
    best_cpu = best_cpu[best_cpu['N'].isin(common_ns)].sort_values('N')
    ax.plot(best_cpu['N'], best_cpu['throughput_Mkeys_s'],
            marker='s', color='#FF9800', label='Best CPU', linewidth=2, markersize=7)

    # CUDA kernel-only
    cuda_kern = cuda[cuda['N'].isin(common_ns)].sort_values('N')
    if 'tput_kernel_Mkeys_s' in cuda_kern.columns:
        vals = pd.to_numeric(cuda_kern['tput_kernel_Mkeys_s'], errors='coerce')
        ax.plot(cuda_kern['N'], vals,
                marker='^', color='#9C27B0', label='CUDA (solo kernel)',
                linewidth=2, markersize=7)

    # CUDA end-to-end
    if 'tput_e2e_Mkeys_s' in cuda_kern.columns:
        vals = pd.to_numeric(cuda_kern['tput_e2e_Mkeys_s'], errors='coerce')
        ax.plot(cuda_kern['N'], vals,
                marker='v', color='#E91E63', label='CUDA (end-to-end)',
                linewidth=2, markersize=7, linestyle='--')

    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('Throughput (Mkeys/s)')
    ax.set_title(f'CPU vs GPU (P={int(P)})')
    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: format_N(int(x))))
    ax.legend()
    save_fig(fig, outdir, '09_cuda_vs_cpu', fmt)


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(description='Genera grafici di analisi per SPM Modulo 1')
    parser.add_argument('--cpu', default='results/cpu_results.csv',
                        help='CSV risultati CPU (default: results/cpu_results.csv)')
    parser.add_argument('--cuda', default='results/cuda_results.csv',
                        help='CSV risultati CUDA (default: results/cuda_results.csv)')
    parser.add_argument('--no-cuda', action='store_true',
                        help='Non generare grafici CUDA')
    parser.add_argument('--outdir', default='results/plots',
                        help='Directory output grafici (default: results/plots)')
    parser.add_argument('--format', default='png', choices=['png', 'pdf', 'svg'],
                        help='Formato immagini (default: png)')
    args = parser.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    # carica CSV
    cpu_df = None
    cuda_df = None

    if os.path.exists(args.cpu):
        cpu_df = pd.read_csv(args.cpu)
        # normalizza nomi implementazione
        cpu_df['impl'] = cpu_df['impl'].str.strip().str.lower()
        cpu_df['key_space'] = pd.to_numeric(cpu_df['key_space'], errors='coerce').fillna(0).astype(int)
        print(f"CPU: {len(cpu_df)} righe da {args.cpu}")
    else:
        print(f"File CPU non trovato: {args.cpu}")
        print("Esegui prima: python3 analysis/parse_results.py")

    if not args.no_cuda and os.path.exists(args.cuda):
        cuda_df = pd.read_csv(args.cuda)
        print(f"CUDA: {len(cuda_df)} righe da {args.cuda}")
    elif not args.no_cuda:
        print(f"File CUDA non trovato: {args.cuda} (CUDA grafici saltati)")

    if cpu_df is None and cuda_df is None:
        print("\nNessun dato disponibile. Esegui prima i benchmark e parse_results.py")
        sys.exit(1)

    print(f"\nGenerazione grafici in {outdir}/")
    print("=" * 50)

    # grafici CPU
    if cpu_df is not None and len(cpu_df) > 0:
        plot_throughput_vs_N(cpu_df, outdir, args.format)
        plot_throughput_vs_P(cpu_df, outdir, args.format)
        plot_speedup_vs_N(cpu_df, outdir, args.format)
        plot_speedup_vs_P(cpu_df, outdir, args.format)
        plot_time_vs_N(cpu_df, outdir, args.format)
        plot_distribution(cpu_df, outdir, args.format)
        plot_keyspace_sensitivity(cpu_df, outdir, args.format)
        plot_summary_table(cpu_df, outdir, args.format)

    # grafici CUDA
    if cuda_df is not None and len(cuda_df) > 0:
        plot_cuda_breakdown(cuda_df, outdir, args.format)
        if cpu_df is not None:
            plot_cuda_vs_cpu(cpu_df, cuda_df, outdir, args.format)

    print("=" * 50)
    print("Fatto!")


if __name__ == '__main__':
    main()
