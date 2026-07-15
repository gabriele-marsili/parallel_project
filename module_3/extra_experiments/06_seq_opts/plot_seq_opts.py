#!/usr/bin/env python3
"""Esp.6 — la superlinearita' apparente e la baseline non ottimizzata.
Figura 1: baseline sequenziale nelle 4 varianti di prefetch + OpenMP a T=1.
Figura 2: efficienza a T basso contro le due baseline."""
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results"); OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

sv = pd.read_csv(os.path.join(RES, "seq_variants.csv"))
lt = pd.read_csv(os.path.join(RES, "omp_lowT.csv"))

seq_plain = sv[(sv.pf_scatter == 0) & (sv.pf_probe == 0)]["total_ms"]
seq_opt = sv[(sv.pf_scatter == 12) & (sv.pf_probe == 8)]["total_ms"]
omp1 = lt[lt.threads == 1]["total_ms"]

fig, ax = plt.subplots(figsize=(8.6, 4.5))
bars = [("sequenziale\nsenza prefetch\n(baseline del report)", seq_plain, "#9E9E9E"),
        ("sequenziale\ncon prefetch scatter", sv[(sv.pf_scatter == 12) & (sv.pf_probe == 0)]["total_ms"], "#64B5F6"),
        ("sequenziale\ncon prefetch probe", sv[(sv.pf_scatter == 0) & (sv.pf_probe == 8)]["total_ms"], "#EF6C00"),
        ("sequenziale\ncon entrambi", seq_opt, "#2E7D32"),
        ("OpenMP loop\na T=1", omp1, "#1565C0")]
x = np.arange(len(bars))
ax.bar(x, [b[1].median() for b in bars], color=[b[2] for b in bars],
       yerr=[b[1].std() for b in bars], capsize=3)
for xi, (_, s, _) in zip(x, bars):
    ax.text(xi, s.median() + 12, f"{s.median():.0f}", ha="center", fontsize=9.5)
ax.set_xticks(x); ax.set_xticklabels([b[0] for b in bars], fontsize=8.8)
ax.set_ylabel("tempo totale (ms)")
ax.set_title("Il rapporto 1.44x a T=1 viene quasi tutto dal prefetch assente nella baseline\n(uniforme, NR=10M, P=128)", fontsize=11.5)
ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "seq_variants.png"), dpi=170)
plt.close(fig)

fig, ax = plt.subplots(figsize=(8.6, 4.5))
ts = sorted(lt.threads.unique())
omp = lt.groupby("threads")["total_ms"].median().reindex(ts)
for base, c, mk, lbl in [(seq_plain.median(), "#9E9E9E", "o", "contro baseline senza prefetch (come il report)"),
                         (seq_opt.median(), "#2E7D32", "s", "contro baseline con le stesse ottimizzazioni")]:
    eff = base / omp.values / np.array(ts)
    ax.plot(ts, eff, marker=mk, color=c, lw=1.8, markersize=7, label=lbl)
    for t, e in zip(ts, eff):
        ax.text(t, e + 0.015, f"{e:.2f}", ha="center", fontsize=9)
ax.axhline(1.0, color="#D32F2F", ls="--", lw=1.4)
ax.text(2.6, 1.03, "efficienza 1 (limite reale)", color="#D32F2F", fontsize=9.5)
ax.set_xticks(ts)
ax.set_xlabel("numero di thread")
ax.set_ylabel("efficienza  T_seq / (T * T_par)")
ax.set_title("Efficienza a T basso: con la baseline ottimizzata la superlinearita sparisce", fontsize=11.5)
ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True); ax.legend(fontsize=9)
ax.set_ylim(0.5, 1.78)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "efficiency.png"), dpi=170)
print("ok ->", OUT)
