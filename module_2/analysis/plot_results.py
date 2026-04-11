#!/usr/bin/env python3
"""
Generate report-ready plots for Module 2

Reads CSV files from results/ and produces publication-quality plots in results/plots/.

Plots generated:
  1. Strong scaling — Speedup vs threads (multiple problem sizes)
  2. Strong scaling — Efficiency vs threads
  3. Strong scaling — Absolute execution time
  4. Weak scaling — WSE vs threads
  5. Phase breakdown — Stacked bar chart
  6. Phase breakdown — Per-phase speedup
  7. Phase breakdown — Percentage composition
  8. Partition sensitivity — Time vs P
  9. Duplicate density — Impact of max_key
 10. Amdahl fit — Estimated serial fraction

Usage:
  python3 analysis/plot_results.py
  python3 analysis/plot_results.py --format pdf
"""

import argparse
import sys
from pathlib import Path

import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
from matplotlib.lines import Line2D

# ── Global style ──
plt.rcParams.update({
    'figure.figsize': (10, 6),
    'figure.dpi': 150,
    'axes.grid': True,
    'grid.alpha': 0.25,
    'grid.linestyle': '--',
    'axes.axisbelow': True,
    'font.family': 'serif',
    'font.size': 11,
    'axes.titlesize': 14,
    'axes.titleweight': 'bold',
    'axes.labelsize': 12,
    'legend.fontsize': 10,
    'legend.framealpha': 0.9,
    'lines.linewidth': 2.2,
    'lines.markersize': 8,
})

C = {  # color palette
    'ideal':   '#BDBDBD',
    'blue':    '#1565C0',
    'green':   '#2E7D32',
    'orange':  '#E65100',
    'red':     '#D32F2F',
    'purple':  '#7B1FA2',
    'teal':    '#00838F',
    'hist_R':  '#42A5F5',
    'scat_R':  '#1E88E5',
    'hist_S':  '#66BB6A',
    'scat_S':  '#43A047',
    'join':    '#EF5350',
}

SIZE_MARKERS = ['o', 's', 'D', '^', 'v']
SIZE_COLORS  = [C['blue'], C['green'], C['orange'], C['red'], C['purple']]


def savefig(fig, name, outdir, fmt):
    path = outdir / f"{name}.{fmt}"
    fig.savefig(path, bbox_inches='tight', dpi=200 if fmt == 'png' else None)
    plt.close(fig)
    print(f"  -> {path}")


# ════════════════════════════════════════════════════════════
# 1-3. STRONG SCALING
# ════════════════════════════════════════════════════════════
def plot_strong_scaling(df, outdir, fmt):
    groups = df.groupby('nr')

    # ── 1. Speedup ──
    fig, ax = plt.subplots()
    max_t = 0
    for idx, (nr, g) in enumerate(groups):
        threads  = g['threads'].values
        speedup  = g['time_seq'].values[0] / g['time_par'].values
        label    = f'NR={nr//1_000_000}M'
        ax.plot(threads, speedup, f'{SIZE_MARKERS[idx]}-', color=SIZE_COLORS[idx], label=label)
        max_t = max(max_t, threads.max())
    ax.plot([1, max_t], [1, max_t], '--', color=C['ideal'], label='Ideal S(p)=p', zorder=0)
    ax.set_xlabel('Number of Threads')
    ax.set_ylabel('Speedup  $S(p) = T_{seq} / T_{par}(p)$')
    ax.set_title('Strong Scaling — Speedup')
    ax.legend()
    ax.set_xlim(0, max_t + 1)
    ax.set_ylim(0, None)
    savefig(fig, 'strong_speedup', outdir, fmt)

    # ── 2. Efficiency ──
    fig, ax = plt.subplots()
    ax.axvspan(16, max_t + 1, alpha=0.08, color='gray', label='HT regime ($p>16$)')
    ax.axhline(y=1.0, ls='--', color=C['ideal'], label='Ideal E=1')
    for idx, (nr, g) in enumerate(groups):
        threads    = g['threads'].values
        speedup    = g['time_seq'].values[0] / g['time_par'].values
        efficiency = speedup / threads
        label      = f'NR={nr//1_000_000}M'
        ax.plot(threads, efficiency, f'{SIZE_MARKERS[idx]}-', color=SIZE_COLORS[idx], label=label)
    ax.set_xlabel('Number of Threads')
    ax.set_ylabel('Efficiency  $E(p) = S(p) / p$')
    ax.set_title('Strong Scaling — Efficiency')
    ax.legend()
    ax.set_xlim(0, max_t + 1)
    ax.set_ylim(0, 1.15)
    ax.yaxis.set_major_formatter(ticker.PercentFormatter(1.0))
    savefig(fig, 'strong_efficiency', outdir, fmt)

    # ── 3. Absolute time ──
    fig, ax = plt.subplots()
    for idx, (nr, g) in enumerate(groups):
        threads = g['threads'].values
        times   = g['time_par'].values * 1000  # ms
        label   = f'NR={nr//1_000_000}M'
        ax.plot(threads, times, f'{SIZE_MARKERS[idx]}-', color=SIZE_COLORS[idx], label=label)
        # ideal line
        ax.plot(threads, times[0] / threads, ':', color=SIZE_COLORS[idx], alpha=0.4)
    ax.set_xlabel('Number of Threads')
    ax.set_ylabel('Execution Time (ms)')
    ax.set_title('Strong Scaling — Execution Time')
    ax.legend()
    ax.set_xlim(0, max_t + 1)
    savefig(fig, 'strong_time', outdir, fmt)

    # ── Summary CSV ──
    rows = []
    for nr, g in groups:
        for _, row in g.iterrows():
            sp = row['time_seq'] / row['time_par']
            rows.append({
                'NR': int(row['nr']), 'threads': int(row['threads']),
                'time_seq_ms': row['time_seq']*1000, 'time_par_ms': row['time_par']*1000,
                'speedup': sp, 'efficiency': sp / row['threads'],
            })
    pd.DataFrame(rows).to_csv(outdir / 'strong_summary.csv', index=False, float_format='%.4f')
    print(f"  -> {outdir / 'strong_summary.csv'}")


# ════════════════════════════════════════════════════════════
# 4. WEAK SCALING
# ════════════════════════════════════════════════════════════
def plot_weak_scaling(df, outdir, fmt):
    threads = df['threads'].values
    times   = df['time_sec'].values
    t1      = times[0]
    wse     = t1 / times

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5.5))

    # WSE
    wse_max = max(wse.max(), 1.0)
    ax1.axhline(y=1.0, ls='--', color=C['ideal'], label='Ideal WSE=1', linewidth=1.5)
    ax1.plot(threads, wse, 'D-', color=C['orange'], label='Measured WSE', markersize=9, zorder=5)
    for i, (t, w) in enumerate(zip(threads, wse)):
        ax1.annotate(f'{w:.2f}', (t, w), textcoords='offset points',
                     xytext=(0, 10), ha='center', fontsize=8, color=C['orange'])
    ax1.set_xlabel('Number of Threads (problem size ∝ p)')
    ax1.set_ylabel('Weak Scaling Efficiency $T(1)/T(p)$')
    ax1.set_title('Weak Scaling — Efficiency')
    ax1.legend(loc='upper right', fontsize=9)
    ax1.set_ylim(0, wse_max * 1.2)
    ax1.set_xlim(0, threads[-1] + 1)
    ax1.yaxis.set_major_formatter(ticker.PercentFormatter(1.0))

    # Absolute time
    ax2.plot(threads, times * 1000, 's-', color=C['teal'], markersize=9)
    ax2.axhline(y=t1 * 1000, ls='--', color=C['ideal'], label='Ideal T=const')
    for i, (t, tm) in enumerate(zip(threads, times)):
        ax2.annotate(f'{tm*1000:.0f}ms', (t, tm*1000), textcoords='offset points',
                     xytext=(0, 10), ha='center', fontsize=8, color=C['teal'])
    ax2.set_xlabel('Number of Threads (problem size ∝ p)')
    ax2.set_ylabel('Execution Time (ms)')
    ax2.set_title('Weak Scaling — Execution Time')
    ax2.legend()
    ax2.set_xlim(0, threads[-1] + 1)

    fig.tight_layout()
    savefig(fig, 'weak_scaling', outdir, fmt)


# ════════════════════════════════════════════════════════════
# 5-7. PHASE BREAKDOWN
# ════════════════════════════════════════════════════════════
def plot_phase_breakdown(df, outdir, fmt):
    # Drop duplicate threads=1 row (seq baseline vs par-1): keep seq (row 0).
    # Applies to all three plots (stacked bar, speedup, percentage).
    df      = df.drop_duplicates(subset='threads', keep='first')
    phases  = ['histogram_R_ms', 'scatter_R_ms', 'histogram_S_ms', 'scatter_S_ms', 'join_local_ms']
    labels  = ['Histogram R', 'Scatter R', 'Histogram S', 'Scatter S', 'Join Local']
    colors  = [C['hist_R'], C['scat_R'], C['hist_S'], C['scat_S'], C['join']]
    threads = df['threads'].values.astype(str)
    x       = np.arange(len(threads))

    # ── 5. Stacked bar ──
    fig, ax = plt.subplots()
    bottom = np.zeros(len(x))
    for phase, label, color in zip(phases, labels, colors):
        vals = df[phase].values
        ax.bar(x, vals, bottom=bottom, label=label, color=color, edgecolor='white', linewidth=0.5)
        bottom += vals
    ax.set_xlabel('Number of Threads')
    ax.set_ylabel('Time (ms)')
    ax.set_title('Phase Breakdown — Execution Time')
    ax.set_xticks(x)
    ax.set_xticklabels(threads)
    ax.legend(loc='upper right')
    savefig(fig, 'phase_breakdown', outdir, fmt)

    # ── 6. Per-phase speedup ──
    fig, ax = plt.subplots()
    t_vals = df['threads'].values
    for phase, label, color in zip(phases, labels, colors):
        vals = df[phase].values
        if vals[0] > 0:
            sp = vals[0] / vals
            ax.plot(t_vals, sp, 'o-', color=color, label=label)
    ax.plot(t_vals, t_vals.astype(float), '--', color=C['ideal'], label='Ideal', linewidth=1.5)
    ax.set_xlabel('Number of Threads')
    ax.set_ylabel('Per-Phase Speedup')
    ax.set_title('Speedup by Phase')
    ax.legend()
    savefig(fig, 'phase_speedup', outdir, fmt)

    # ── 7. Percentage composition ──
    fig, ax = plt.subplots()
    total = df['total_ms'].values
    bottom = np.zeros(len(x))
    for phase, label, color in zip(phases, labels, colors):
        vals = df[phase].values / total * 100
        ax.bar(x, vals, bottom=bottom, label=label, color=color, edgecolor='white', linewidth=0.5)
        bottom += vals
    ax.set_xlabel('Number of Threads')
    ax.set_ylabel('Percentage of Total Time (%)')
    ax.set_title('Phase Breakdown — Relative Composition')
    ax.set_xticks(x)
    ax.set_xticklabels(threads)
    ax.set_ylim(0, 105)
    ax.legend(loc='upper right')
    savefig(fig, 'phase_percentage', outdir, fmt)


# ════════════════════════════════════════════════════════════
# 8. PARTITION SENSITIVITY
# ════════════════════════════════════════════════════════════
def plot_partition_sensitivity(df, outdir, fmt):
    fig, ax = plt.subplots()
    P_vals = df['P'].values
    times  = df['time_sec'].values * 1000
    ax.plot(P_vals, times, 'o-', color=C['purple'], markersize=9)
    ax.set_xlabel('Number of Partitions (P)')
    ax.set_ylabel('Execution Time (ms)')
    ax.set_title(f'Partition Count Sensitivity (t={df["threads"].iloc[0]})')
    ax.set_xscale('log', base=2)
    ax.set_xticks(P_vals)
    ax.get_xaxis().set_major_formatter(ticker.ScalarFormatter())
    savefig(fig, 'partition_sensitivity', outdir, fmt)


# ════════════════════════════════════════════════════════════
# 9. DUPLICATE DENSITY
# ════════════════════════════════════════════════════════════
def plot_duplicate_density(df, outdir, fmt):
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(7, 7))

    mk     = df['max_key'].values
    t_seq  = df['time_seq'].values * 1000
    t_par  = df['time_par'].values * 1000
    sp     = df['time_seq'].values / df['time_par'].values

    ax1.plot(mk, t_seq, 'o-', color=C['blue'], label='Sequential')
    ax1.plot(mk, t_par, 's-', color=C['orange'], label=f'Parallel (p={df["threads"].iloc[0]})')
    ax1.set_xlabel('max\_key  (key range $\propto$ $1/$duplicate density)')
    ax1.set_ylabel('Execution Time (ms)')
    ax1.set_title('Impact of Duplicate Density')
    ax1.set_xscale('log')
    ax1.legend()

    ax2.plot(mk, sp, 'D-', color=C['green'], markersize=8)
    for x, y in zip(mk, sp):
        ax2.annotate(f'{y:.2f}×', (x, y), textcoords='offset points',
                     xytext=(0, 8), ha='center', fontsize=8, color=C['green'])
    ax2.set_xlabel('max\_key')
    ax2.set_ylabel('Speedup $S(p)$')
    ax2.set_title('Speedup vs Duplicate Density')
    ax2.set_xscale('log')
    ax2.set_ylim(0, sp.max() * 1.2)

    fig.tight_layout()
    savefig(fig, 'duplicate_density', outdir, fmt)


# ════════════════════════════════════════════════════════════
# 10. AMDAHL FIT
# ════════════════════════════════════════════════════════════
def plot_amdahl_fit(df, outdir, fmt):
    """Fit Amdahl's law S(p) = 1/(f + (1-f)/p) to estimate serial fraction f."""
    # Use largest problem size for the best fit
    biggest = df[df['nr'] == df['nr'].max()].copy()
    threads = biggest['threads'].values.astype(float)
    speedup = biggest['time_seq'].values[0] / biggest['time_par'].values

    # Least-squares fit for f: minimize sum((S_measured - 1/(f+(1-f)/p))^2)
    from scipy.optimize import curve_fit
    def amdahl(p, f):
        return 1.0 / (f + (1.0 - f) / p)

    try:
        popt, _ = curve_fit(amdahl, threads, speedup, p0=[0.05], bounds=(0, 1))
        f_est = popt[0]
    except Exception:
        f_est = None

    fig, ax = plt.subplots()
    p_fine = np.linspace(1, threads.max() * 1.1, 200)
    ax.plot(p_fine, p_fine, '--', color=C['ideal'], label='Ideal S=p', alpha=0.5)
    ax.plot(threads, speedup, 'o', color=C['blue'], markersize=10, label='Measured', zorder=5)
    if f_est is not None:
        ax.plot(p_fine, amdahl(p_fine, f_est), '-', color=C['red'],
                label=f'Amdahl fit (f={f_est:.4f}, S_max={1/f_est:.1f})')
    ax.set_xlabel('Number of Threads')
    ax.set_ylabel('Speedup')
    ax.set_title('Amdahl\'s Law Fit — Serial Fraction Estimation')
    ax.legend()
    ax.set_xlim(0, threads.max() + 1)
    ax.set_ylim(0, None)
    savefig(fig, 'amdahl_fit', outdir, fmt)

    if f_est is not None:
        print(f"  Estimated serial fraction f = {f_est:.4f} -> max speedup = {1/f_est:.1f}x")


# ════════════════════════════════════════════════════════════
# 4b. WEAK SCALING 5M
# ════════════════════════════════════════════════════════════
def plot_weak_scaling_5M(df, outdir, fmt):
    threads = df['threads'].values
    times   = df['time_sec'].values
    t1      = times[0]
    wse     = t1 / times

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5.5))

    wse_max = max(wse.max(), 1.0)
    ax1.axhline(y=1.0, ls='--', color=C['ideal'], label='Ideal WSE=1', linewidth=1.5)
    ax1.plot(threads, wse, 'D-', color=C['blue'], label='Measured WSE (NR_BASE=5M)', markersize=9, zorder=5)
    for t, w in zip(threads, wse):
        ax1.annotate(f'{w:.2f}', (t, w), textcoords='offset points',
                     xytext=(0, 10), ha='center', fontsize=8, color=C['blue'])
    ax1.set_xlabel('Number of Threads (problem size ∝ p)')
    ax1.set_ylabel('Weak Scaling Efficiency $T(1)/T(p)$')
    ax1.set_title('Weak Scaling — Efficiency (NR$_{base}$=5M)')
    ax1.legend(loc='upper right', fontsize=9)
    ax1.set_ylim(0, wse_max * 1.2)
    ax1.set_xlim(0, threads[-1] + 1)
    ax1.yaxis.set_major_formatter(ticker.PercentFormatter(1.0))

    ax2.plot(threads, times * 1000, 's-', color=C['blue'], markersize=9)
    ax2.axhline(y=t1 * 1000, ls='--', color=C['ideal'], label='Ideal T=const')
    for t, tm in zip(threads, times):
        ax2.annotate(f'{tm*1000:.0f}ms', (t, tm*1000), textcoords='offset points',
                     xytext=(0, 10), ha='center', fontsize=8, color=C['blue'])
    ax2.set_xlabel('Number of Threads (problem size ∝ p)')
    ax2.set_ylabel('Execution Time (ms)')
    ax2.set_title('Weak Scaling — Execution Time (NR$_{base}$=5M)')
    ax2.legend()
    ax2.set_xlim(0, threads[-1] + 1)

    fig.tight_layout()
    savefig(fig, 'weak_scaling_5M', outdir, fmt)


# ════════════════════════════════════════════════════════════
# 4c. WEAK SCALING COMPARISON (1M vs 5M base)
# ════════════════════════════════════════════════════════════
def plot_weak_scaling_compare(df1, df5, outdir, fmt):
    t1_1M = df1['time_sec'].values[0]
    t1_5M = df5['time_sec'].values[0]
    wse_1M = t1_1M / df1['time_sec'].values
    wse_5M = t1_5M / df5['time_sec'].values

    fig, ax = plt.subplots()
    ax.axhline(y=1.0, ls='--', color=C['ideal'], label='Ideal WSE=1', linewidth=1.5)
    ax.plot(df1['threads'].values, wse_1M, 'D-', color=C['orange'],
            label='NR$_{base}$=1M/thread', markersize=9)
    ax.plot(df5['threads'].values, wse_5M, 's-', color=C['blue'],
            label='NR$_{base}$=5M/thread', markersize=9)
    ax.set_xlabel('Number of Threads (problem size ∝ p)')
    ax.set_ylabel('Weak Scaling Efficiency $T(1)/T(p)$')
    ax.set_title('Weak Scaling Efficiency — Workload Size Comparison')
    ax.legend()
    ax.set_ylim(0, max(wse_1M.max(), wse_5M.max()) * 1.25)
    ax.set_xlim(0, max(df1['threads'].max(), df5['threads'].max()) + 1)
    ax.yaxis.set_major_formatter(ticker.PercentFormatter(1.0))
    savefig(fig, 'weak_scaling_compare', outdir, fmt)


# ════════════════════════════════════════════════════════════
# MAIN
# ════════════════════════════════════════════════════════════
def main():
    parser = argparse.ArgumentParser(description='Plot Module 2 results')
    parser.add_argument('--results', default='results', help='Results directory')
    parser.add_argument('--format', default='png', choices=['png', 'pdf', 'svg'])
    args = parser.parse_args()

    resdir = Path(args.results)
    outdir = resdir / 'plots'
    outdir.mkdir(parents=True, exist_ok=True)
    fmt = args.format
    print(f"[*] Reading from {resdir}, writing to {outdir} ({fmt})")

    # Strong scaling
    p = resdir / 'strong_scaling.csv'
    if p.exists():
        df = pd.read_csv(p)
        print("[1] Strong scaling plots...")
        plot_strong_scaling(df, outdir, fmt)
        try:
            print("[10] Amdahl fit...")
            plot_amdahl_fit(df, outdir, fmt)
        except ImportError:
            print("  (scipy not available — skipping Amdahl fit)")
    else:
        print(f"[!] {p} not found")

    # Weak scaling
    p = resdir / 'weak_scaling.csv'
    if p.exists():
        df = pd.read_csv(p)
        print("[2] Weak scaling plot (NR_BASE=1M)...")
        plot_weak_scaling(df, outdir, fmt)

    # Weak scaling 5M — larger base, better WSE curve
    p5 = resdir / 'weak_scaling_5M.csv'
    if p5.exists():
        df5 = pd.read_csv(p5)
        print("[2b] Weak scaling plot (NR_BASE=5M)...")
        plot_weak_scaling_5M(df5, outdir, fmt)

    # Combined comparison plot if both exist
    if p.exists() and p5.exists():
        print("[2c] Weak scaling comparison plot...")
        plot_weak_scaling_compare(pd.read_csv(p), df5, outdir, fmt)

    # Phase breakdown
    p = resdir / 'phase_breakdown.csv'
    if p.exists():
        df = pd.read_csv(p)
        print("[3] Phase breakdown plots...")
        plot_phase_breakdown(df, outdir, fmt)

    # Partition sensitivity
    p = resdir / 'partition_sensitivity.csv'
    if p.exists():
        df = pd.read_csv(p)
        print("[4] Partition sensitivity...")
        plot_partition_sensitivity(df, outdir, fmt)

    # Duplicate density
    p = resdir / 'duplicate_density.csv'
    if p.exists():
        df = pd.read_csv(p)
        print("[5] Duplicate density...")
        plot_duplicate_density(df, outdir, fmt)

    print("\n[✓] Done!")


if __name__ == '__main__':
    main()
