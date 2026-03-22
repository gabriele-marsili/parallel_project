#!/usr/bin/env python3
"""
plot_results.py — Genera grafici di analisi per SPM Modulo 1.

Legge i CSV prodotti da parse_results.py e genera grafici in results/plots/.

Uso:
  python3 analysis/plot_results.py                    # default
  python3 analysis/plot_results.py --cpu cpu.csv      # file specifici
  python3 analysis/plot_results.py --no-cuda          # salta CUDA
  python3 analysis/plot_results.py --format pdf       # vettoriale per LaTeX
"""

import argparse
import sys
import os
from pathlib import Path

import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

# ---- stile globale ----
plt.rcParams.update({
    'figure.figsize': (10, 6),
    'figure.dpi': 150,
    'axes.grid': True,
    'grid.alpha': 0.3,
    'grid.linestyle': '--',
    'axes.axisbelow': True,
    'font.size': 11,
    'axes.titlesize': 14,
    'axes.titleweight': 'bold',
    'axes.labelsize': 12,
    'legend.fontsize': 10,
    'legend.framealpha': 0.9,
    'lines.linewidth': 2.2,
    'lines.markersize': 8,
})

# colori e stili per implementazione
STYLE = {
    'baseline': {'color': '#1565C0', 'marker': 'o', 'label': 'Baseline (no-vec)'},
    'autovec':  {'color': '#2E7D32', 'marker': 's', 'label': 'Auto-vectorized'},
    'avx2':     {'color': '#E65100', 'marker': 'D', 'label': 'AVX2 intrinsics'},
    'cuda':     {'color': '#7B1FA2', 'marker': '^', 'label': 'CUDA (GPU)'},
}

IMPL_ORDER = ['baseline', 'autovec', 'avx2', 'cuda']


def format_N(n):
    """1000000 -> '1M', 10000000 -> '10M'."""
    if n >= 1e9: return f'{n/1e9:.0f}G'
    if n >= 1e6: return f'{n/1e6:.0f}M'
    if n >= 1e3: return f'{n/1e3:.0f}K'
    return str(int(n))


def save_fig(fig, outdir, name, fmt):
    path = outdir / f'{name}.{fmt}'
    fig.savefig(path, bbox_inches='tight', facecolor='white')
    plt.close(fig)
    print(f'  {path}')


def get_style(impl):
    return STYLE.get(impl, {'color': '#757575', 'marker': 'x', 'label': impl})


def dedup(df, group_cols, agg_cols=None):
    """Deduplica: una riga per combinazione di group_cols, prendendo la mediana."""
    if agg_cols is None:
        agg_cols = {'median_ms': 'median', 'stddev_ms': 'median',
                    'throughput_Mkeys_s': 'median'}
    return df.groupby(group_cols, as_index=False).agg(agg_cols)


def sweep_N_data(df):
    """Estrae dati per lo sweep N: key_space=0, P fisso, deduplicati."""
    sweep = df[df['key_space'] == 0].copy()
    if sweep.empty:
        return sweep, 0
    main_P = sweep['P'].mode().iloc[0] if not sweep['P'].mode().empty else 256
    sweep = sweep[sweep['P'] == main_P]
    sweep = dedup(sweep, ['impl', 'N'])
    return sweep, main_P


def sweep_P_data(df):
    """Estrae dati per lo sweep P: key_space=0, N fisso, deduplicati."""
    sweep = df[df['key_space'] == 0].copy()
    if sweep.empty:
        return sweep, 0
    main_N = sweep['N'].max()
    sweep = sweep[sweep['N'] == main_N]
    if len(sweep['P'].unique()) < 3:
        return pd.DataFrame(), main_N
    sweep = dedup(sweep, ['impl', 'P'])
    return sweep, main_N


# ============================================================================
# Grafici CPU
# ============================================================================

def plot_throughput_vs_N(df, outdir, fmt):
    """1: Throughput al variare di N."""
    sweep, main_P = sweep_N_data(df)
    if sweep.empty:
        return

    fig, ax = plt.subplots()
    for impl in IMPL_ORDER:
        sub = sweep[sweep['impl'] == impl].sort_values('N')
        if sub.empty:
            continue
        s = get_style(impl)
        ax.plot(sub['N'], sub['throughput_Mkeys_s'],
                marker=s['marker'], color=s['color'], label=s['label'])

    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('Throughput (Mkeys/s)')
    ax.set_title(f'Throughput vs dimensione input (P={main_P})')
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: format_N(int(x))))
    ax.legend(loc='best')
    save_fig(fig, outdir, '01_throughput_vs_N', fmt)


def plot_throughput_vs_P(df, outdir, fmt):
    """2: Throughput al variare di P."""
    sweep, main_N = sweep_P_data(df)
    if sweep.empty:
        return

    fig, ax = plt.subplots()
    for impl in IMPL_ORDER:
        sub = sweep[sweep['impl'] == impl].sort_values('P')
        if sub.empty:
            continue
        s = get_style(impl)
        ax.plot(sub['P'], sub['throughput_Mkeys_s'],
                marker=s['marker'], color=s['color'], label=s['label'])

    ax.set_xlabel('P (numero di partizioni)')
    ax.set_ylabel('Throughput (Mkeys/s)')
    ax.set_title(f'Throughput vs partizioni (N={format_N(main_N)})')
    ax.set_xscale('log', base=2)
    ax.xaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.legend(loc='best')
    save_fig(fig, outdir, '02_throughput_vs_P', fmt)


def plot_speedup_vs_N(df, outdir, fmt):
    """3: Speedup rispetto al baseline al variare di N."""
    sweep, main_P = sweep_N_data(df)
    if sweep.empty:
        return

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
        s = get_style(impl)
        ax.plot(merged['N'], merged['speedup'],
                marker=s['marker'], color=s['color'], label=s['label'])

    ax.axhline(y=1.0, color='gray', linestyle='--', alpha=0.5, label='Baseline (1.0×)')
    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('Speedup (vs baseline)')
    ax.set_title(f'Speedup vs dimensione input (P={main_P})')
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: format_N(int(x))))
    ax.legend(loc='best')
    save_fig(fig, outdir, '03_speedup_vs_N', fmt)


def plot_speedup_vs_P(df, outdir, fmt):
    """4: Speedup rispetto al baseline al variare di P."""
    sweep, main_N = sweep_P_data(df)
    if sweep.empty:
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
        s = get_style(impl)
        ax.plot(merged['P'], merged['speedup'],
                marker=s['marker'], color=s['color'], label=s['label'])

    ax.axhline(y=1.0, color='gray', linestyle='--', alpha=0.5, label='Baseline (1.0×)')
    ax.set_xlabel('P (numero di partizioni)')
    ax.set_ylabel('Speedup (vs baseline)')
    ax.set_title(f'Speedup vs partizioni (N={format_N(main_N)})')
    ax.set_xscale('log', base=2)
    ax.xaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.legend(loc='best')
    save_fig(fig, outdir, '04_speedup_vs_P', fmt)


def plot_time_vs_N(df, outdir, fmt):
    """5: Tempo mediano con barre di errore (stddev)."""
    sweep, main_P = sweep_N_data(df)
    if sweep.empty:
        return

    fig, ax = plt.subplots()
    for impl in IMPL_ORDER:
        sub = sweep[sweep['impl'] == impl].sort_values('N')
        if sub.empty:
            continue
        s = get_style(impl)
        ax.errorbar(sub['N'], sub['median_ms'], yerr=sub['stddev_ms'],
                    marker=s['marker'], color=s['color'], label=s['label'],
                    capsize=4, capthick=1.5)

    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('Tempo mediano (ms)')
    ax.set_title(f'Tempo di esecuzione vs dimensione input (P={main_P})')
    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: format_N(int(x))))
    ax.legend(loc='best')
    save_fig(fig, outdir, '05_time_vs_N', fmt)


def plot_distribution(df, outdir, fmt):
    """6: Qualità distribuzione hash (max/atteso) al variare di N."""
    sweep = df[(df['key_space'] == 0) & (df['dist_ratio'] != '')].copy()
    if sweep.empty:
        return

    sweep['dist_ratio'] = pd.to_numeric(sweep['dist_ratio'])
    main_P = sweep['P'].mode().iloc[0] if not sweep['P'].mode().empty else 256
    sweep = sweep[sweep['P'] == main_P]
    sweep = sweep.groupby(['impl', 'N'], as_index=False).agg({'dist_ratio': 'mean'})

    fig, ax = plt.subplots()
    for impl in IMPL_ORDER:
        sub = sweep[sweep['impl'] == impl].sort_values('N')
        if sub.empty:
            continue
        s = get_style(impl)
        ax.plot(sub['N'], sub['dist_ratio'],
                marker=s['marker'], color=s['color'], label=s['label'])

    ax.axhline(y=1.0, color='red', linestyle='--', alpha=0.6, linewidth=1.5,
               label='Distribuzione perfetta')
    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('max(count) / atteso')
    ax.set_title(f'Qualità distribuzione hash (P={main_P})')
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: format_N(int(x))))
    ax.set_ylim(bottom=0.99)
    ax.legend(loc='best')
    save_fig(fig, outdir, '06_distribution_quality', fmt)


def plot_keyspace_sensitivity(df, outdir, fmt):
    """7: Throughput al variare del key_space (duplicati)."""
    has_ks = df[df['key_space'] > 0]
    if has_ks.empty:
        return

    exp_N = has_ks['N'].mode().iloc[0]
    exp_P = has_ks['P'].mode().iloc[0]
    sweep = df[(df['N'] == exp_N) & (df['P'] == exp_P)].copy()
    sweep = sweep.drop_duplicates(subset=['impl', 'key_space'], keep='last')
    if len(sweep['key_space'].unique()) < 3:
        return

    ks_vals = sorted(sweep['key_space'].unique())
    ks_labels = []
    for ks in ks_vals:
        ks_labels.append('full\n(2⁶⁴)' if ks == 0 else format_N(int(ks)))
    ks_to_x = {ks: i for i, ks in enumerate(ks_vals)}

    fig, ax = plt.subplots()
    for impl in IMPL_ORDER:
        sub = sweep[sweep['impl'] == impl].sort_values('key_space')
        if sub.empty:
            continue
        x_pos = [ks_to_x[ks] for ks in sub['key_space']]
        s = get_style(impl)
        ax.plot(x_pos, sub['throughput_Mkeys_s'].values,
                marker=s['marker'], color=s['color'], label=s['label'])

    ax.set_xticks(range(len(ks_vals)))
    ax.set_xticklabels(ks_labels)
    ax.set_xlabel('Key space (universo chiavi)')
    ax.set_ylabel('Throughput (Mkeys/s)')
    ax.set_title(f'Sensibilità ai duplicati (N={format_N(int(exp_N))}, P={int(exp_P)})')
    ax.legend(loc='best')
    save_fig(fig, outdir, '07_keyspace_sensitivity', fmt)


def plot_stddev_analysis(df, outdir, fmt):
    """8: Coefficiente di variazione (stddev/median) per valutare stabilità misure."""
    sweep, main_P = sweep_N_data(df)
    if sweep.empty:
        return

    sweep['cv_pct'] = (sweep['stddev_ms'] / sweep['median_ms']) * 100

    fig, ax = plt.subplots()
    for impl in IMPL_ORDER:
        sub = sweep[sweep['impl'] == impl].sort_values('N')
        if sub.empty:
            continue
        s = get_style(impl)
        ax.plot(sub['N'], sub['cv_pct'],
                marker=s['marker'], color=s['color'], label=s['label'])

    ax.axhline(y=5.0, color='red', linestyle='--', alpha=0.5, label='Soglia 5%')
    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('Coefficiente di variazione (%)')
    ax.set_title(f'Stabilità delle misurazioni (P={main_P})')
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: format_N(int(x))))
    ax.legend(loc='best')
    save_fig(fig, outdir, '08_measurement_stability', fmt)


def plot_bandwidth_utilization(df, outdir, fmt):
    """9: Bandwidth effettiva vs N (bytes letti+scritti / tempo)."""
    sweep, main_P = sweep_N_data(df)
    if sweep.empty:
        return

    # bytes per iterazione: legge 8B (key) + scrive 4B (part_id) = 12B/key
    sweep = sweep.copy()
    sweep['bw_GBs'] = (sweep['N'] * 12) / (sweep['median_ms'] * 1e6)  # GB/s

    fig, ax = plt.subplots()
    for impl in IMPL_ORDER:
        sub = sweep[sweep['impl'] == impl].sort_values('N')
        if sub.empty:
            continue
        s = get_style(impl)
        ax.plot(sub['N'], sub['bw_GBs'],
                marker=s['marker'], color=s['color'], label=s['label'])

    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('Bandwidth effettiva (GB/s)')
    ax.set_title(f'Utilizzo bandwidth memoria (P={main_P})')
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: format_N(int(x))))
    ax.legend(loc='best')
    save_fig(fig, outdir, '09_bandwidth_utilization', fmt)


def plot_time_per_key(df, outdir, fmt):
    """10: Tempo per chiave (ns/key) — normalizzato, confronto diretto."""
    sweep, main_P = sweep_N_data(df)
    if sweep.empty:
        return

    sweep = sweep.copy()
    sweep['ns_per_key'] = (sweep['median_ms'] * 1e6) / sweep['N']

    fig, ax = plt.subplots()
    for impl in IMPL_ORDER:
        sub = sweep[sweep['impl'] == impl].sort_values('N')
        if sub.empty:
            continue
        s = get_style(impl)
        ax.plot(sub['N'], sub['ns_per_key'],
                marker=s['marker'], color=s['color'], label=s['label'])

    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('Tempo per chiave (ns/key)')
    ax.set_title(f'Costo per chiave vs dimensione input (P={main_P})')
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: format_N(int(x))))
    ax.legend(loc='best')
    save_fig(fig, outdir, '10_time_per_key', fmt)


def plot_summary_table(df, outdir, fmt):
    """11: Tabella riepilogativa."""
    sub = df[(df['key_space'] == 0)].copy()
    if sub.empty:
        return

    # trova la configurazione "reference": N più grande, P più comune
    n_counts = sub.groupby('N')['impl'].nunique()
    best_N = n_counts[n_counts == n_counts.max()].index.max()
    main_P = sub[sub['N'] == best_N]['P'].mode().iloc[0]
    sub = sub[(sub['N'] == best_N) & (sub['P'] == main_P)]

    sub = sub.groupby('impl').agg({
        'median_ms': 'median',
        'stddev_ms': 'median',
        'throughput_Mkeys_s': 'median',
    }).reset_index()

    if sub.empty:
        return

    base_ms = sub.loc[sub['impl'] == 'baseline', 'median_ms'].values
    base_ms = base_ms[0] if len(base_ms) > 0 else None

    # ordine fisso
    sub['_order'] = sub['impl'].map({v: i for i, v in enumerate(IMPL_ORDER)})
    sub = sub.sort_values('_order').drop(columns='_order')

    table_data = []
    for _, row in sub.iterrows():
        s = get_style(row['impl'])
        speedup = f"{base_ms / row['median_ms']:.2f}×" if base_ms else "—"
        cv = f"{row['stddev_ms']/row['median_ms']*100:.1f}%"
        ns_key = f"{row['median_ms']*1e6/best_N:.2f}"
        table_data.append([
            s['label'],
            f"{row['median_ms']:.3f}",
            f"{row['stddev_ms']:.3f}",
            cv,
            f"{row['throughput_Mkeys_s']:.1f}",
            ns_key,
            speedup,
        ])

    col_labels = ['Implementazione', 'Mediana\n(ms)', 'Stddev\n(ms)',
                  'CV', 'Throughput\n(Mkeys/s)', 'ns/key', 'Speedup']

    fig, ax = plt.subplots(figsize=(12, 1.8 + len(table_data) * 0.6))
    ax.axis('off')
    table = ax.table(
        cellText=table_data,
        colLabels=col_labels,
        loc='center',
        cellLoc='center',
    )
    table.auto_set_font_size(False)
    table.set_fontsize(11)
    table.auto_set_column_width(col=list(range(len(col_labels))))
    table.scale(1.1, 1.8)

    # stile header
    for j in range(len(col_labels)):
        table[0, j].set_facecolor('#263238')
        table[0, j].set_text_props(color='white', fontweight='bold')
    # colora righe alternate
    for i in range(1, len(table_data) + 1):
        bg = '#ECEFF1' if i % 2 == 0 else 'white'
        for j in range(len(col_labels)):
            table[i, j].set_facecolor(bg)

    ax.set_title(f'Riepilogo (N={format_N(int(best_N))}, P={int(main_P)})',
                 fontsize=14, fontweight='bold', pad=20)
    save_fig(fig, outdir, '11_summary_table', fmt)


# ============================================================================
# Grafici CUDA
# ============================================================================

def plot_cuda_breakdown(cuda_df, outdir, fmt):
    """12: Stacked bar tempi CUDA (H2D / kernel / D2H)."""
    sweep = cuda_df.copy()
    if sweep.empty:
        return

    main_P = sweep['P'].mode().iloc[0] if not sweep['P'].mode().empty else 256
    sweep = sweep[sweep['P'] == main_P].sort_values('N')
    if sweep.empty:
        return

    x_labels = [format_N(int(n)) for n in sweep['N']]
    x = np.arange(len(x_labels))
    width = 0.5

    h2d = sweep['h2d_ms'].astype(float).values
    kern = sweep['kernel_ms'].astype(float).values
    d2h = sweep['d2h_ms'].astype(float).values

    fig, ax = plt.subplots()
    ax.bar(x, h2d, width, label='H→D transfer', color='#EF5350')
    ax.bar(x, kern, width, bottom=h2d, label='Kernel', color='#66BB6A')
    ax.bar(x, d2h, width, bottom=h2d + kern, label='D→H transfer', color='#42A5F5')

    # etichette percentuale sul kernel
    for i in range(len(x)):
        total = h2d[i] + kern[i] + d2h[i]
        if total > 0:
            pct = kern[i] / total * 100
            ax.text(x[i], h2d[i] + kern[i]/2, f'{pct:.1f}%',
                    ha='center', va='center', fontsize=9, fontweight='bold')

    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('Tempo (ms)')
    ax.set_title(f'CUDA: breakdown tempi (P={int(main_P)})')
    ax.set_xticks(x)
    ax.set_xticklabels(x_labels)
    ax.legend()
    save_fig(fig, outdir, '12_cuda_breakdown', fmt)


def plot_cuda_vs_cpu(cpu_df, cuda_df, outdir, fmt):
    """13: Confronto throughput GPU (kernel e e2e) vs best CPU."""
    cpu = cpu_df[(cpu_df['key_space'] == 0)].copy()
    cuda = cuda_df.copy()
    if cpu.empty or cuda.empty:
        return

    cpu_ns = set(cpu['N'].unique())
    cuda_ns = set(cuda['N'].unique())
    common_ns = sorted(cpu_ns & cuda_ns)
    if not common_ns:
        return

    main_P_cpu = cpu['P'].mode().iloc[0]
    main_P_cuda = cuda['P'].mode().iloc[0]
    if main_P_cpu != main_P_cuda:
        return

    P = main_P_cpu
    cpu = cpu[cpu['P'] == P]
    cuda = cuda[cuda['P'] == P]

    fig, ax = plt.subplots()

    # best CPU per N
    best_cpu = cpu.groupby('N')['throughput_Mkeys_s'].max().reset_index()
    best_cpu = best_cpu[best_cpu['N'].isin(common_ns)].sort_values('N')
    ax.plot(best_cpu['N'], best_cpu['throughput_Mkeys_s'],
            marker='s', color='#E65100', label='Best CPU', linewidth=2.2, markersize=8)

    cuda_kern = cuda[cuda['N'].isin(common_ns)].sort_values('N')
    if 'tput_kernel_Mkeys_s' in cuda_kern.columns:
        vals = pd.to_numeric(cuda_kern['tput_kernel_Mkeys_s'], errors='coerce')
        ax.plot(cuda_kern['N'], vals, marker='^', color='#7B1FA2',
                label='CUDA (solo kernel)', linewidth=2.2, markersize=8)
    if 'tput_e2e_Mkeys_s' in cuda_kern.columns:
        vals = pd.to_numeric(cuda_kern['tput_e2e_Mkeys_s'], errors='coerce')
        ax.plot(cuda_kern['N'], vals, marker='v', color='#C2185B',
                label='CUDA (end-to-end)', linewidth=2.2, markersize=8, linestyle='--')

    ax.set_xlabel('N (numero di chiavi)')
    ax.set_ylabel('Throughput (Mkeys/s)')
    ax.set_title(f'CPU vs GPU (P={int(P)})')
    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: format_N(int(x))))
    ax.legend()
    save_fig(fig, outdir, '13_cuda_vs_cpu', fmt)


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(description='Genera grafici per SPM Modulo 1')
    parser.add_argument('--cpu', default='results/cpu_results.csv')
    parser.add_argument('--cuda', default='results/cuda_results.csv')
    parser.add_argument('--no-cuda', action='store_true')
    parser.add_argument('--outdir', default='results/plots')
    parser.add_argument('--format', default='png', choices=['png', 'pdf', 'svg'])
    args = parser.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    cpu_df = None
    cuda_df = None

    if os.path.exists(args.cpu):
        cpu_df = pd.read_csv(args.cpu)
        cpu_df['impl'] = cpu_df['impl'].str.strip().str.lower()
        cpu_df['key_space'] = pd.to_numeric(cpu_df['key_space'], errors='coerce').fillna(0).astype(int)
        print(f"CPU: {len(cpu_df)} righe da {args.cpu}")
    else:
        print(f"File CPU non trovato: {args.cpu}")

    if not args.no_cuda and os.path.exists(args.cuda):
        cuda_df = pd.read_csv(args.cuda)
        print(f"CUDA: {len(cuda_df)} righe da {args.cuda}")
    elif not args.no_cuda:
        print(f"File CUDA non trovato: {args.cuda} (saltato)")

    if cpu_df is None and cuda_df is None:
        print("\nNessun dato. Esegui prima i benchmark e parse_results.py")
        sys.exit(1)

    print(f"\nGenerazione grafici in {outdir}/")
    print("=" * 50)

    if cpu_df is not None and len(cpu_df) > 0:
        plot_throughput_vs_N(cpu_df, outdir, args.format)
        plot_throughput_vs_P(cpu_df, outdir, args.format)
        plot_speedup_vs_N(cpu_df, outdir, args.format)
        plot_speedup_vs_P(cpu_df, outdir, args.format)
        plot_time_vs_N(cpu_df, outdir, args.format)
        plot_distribution(cpu_df, outdir, args.format)
        plot_keyspace_sensitivity(cpu_df, outdir, args.format)
        plot_stddev_analysis(cpu_df, outdir, args.format)
        plot_bandwidth_utilization(cpu_df, outdir, args.format)
        plot_time_per_key(cpu_df, outdir, args.format)
        plot_summary_table(cpu_df, outdir, args.format)

    if cuda_df is not None and len(cuda_df) > 0:
        plot_cuda_breakdown(cuda_df, outdir, args.format)
        if cpu_df is not None:
            plot_cuda_vs_cpu(cpu_df, cuda_df, outdir, args.format)

    print("=" * 50)
    print("Fatto!")


if __name__ == '__main__':
    main()
