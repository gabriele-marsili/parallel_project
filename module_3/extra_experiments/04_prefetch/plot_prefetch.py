#!/usr/bin/env python3
"""Esp.4 — prefetch software: ablation per fase e sweep delle distanze.
Figura 1: ablation 2x2 (scatter e probe on/off) per T in {1,8,16}.
Figura 2: sweep della distanza nello scatter.
Figura 3: sweep della distanza nel probe."""
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

ab = pd.read_csv(os.path.join(RES, "prefetch_ablation.csv"))
ab["scatter"] = ab.scatter_r_ms + ab.scatter_s_ms

fig, axes = plt.subplots(1, 3, figsize=(12.5, 4.3))
CFGS = [(0, 0, "#9E9E9E", "nessun prefetch"),
        (12, 0, "#64B5F6", "solo scatter (12)"),
        (0, 8, "#EF6C00", "solo probe (8)"),
        (12, 8, "#2E7D32", "entrambi (consegnato)")]
for ax, T in zip(axes, [1, 8, 16]):
    sub = ab[ab.threads == T]
    x = np.arange(len(CFGS))
    med = [sub[(sub.pf_scatter == s) & (sub.pf_probe == p)]["total_ms"].median() for s, p, _, _ in CFGS]
    std = [sub[(sub.pf_scatter == s) & (sub.pf_probe == p)]["total_ms"].std() for s, p, _, _ in CFGS]
    ax.bar(x, med, color=[c for _, _, c, _ in CFGS], yerr=std, capsize=3)
    for xi, v in zip(x, med):
        ax.text(xi, v * 1.01, f"{v:.0f}", ha="center", fontsize=9)
    ax.set_xticks(x)
    ax.set_xticklabels([lbl for _, _, _, lbl in CFGS], rotation=20, ha="right", fontsize=8.5)
    ax.set_title(f"T = {T}", fontsize=11)
    ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True)
axes[0].set_ylabel("tempo totale (ms)")
fig.suptitle("Contributo dei due prefetch al tempo totale (uniforme, P=128)", fontsize=12.5)
fig.tight_layout(rect=[0, 0, 1, 0.92])
fig.savefig(os.path.join(OUT, "prefetch_ablation.png"), dpi=170)
plt.close(fig)

ss = pd.read_csv(os.path.join(RES, "pf_scatter_sweep.csv"))
ss["scatter"] = ss.scatter_r_ms + ss.scatter_s_ms
fig, axes = plt.subplots(1, 2, figsize=(11, 4.3))
for ax, T in zip(axes, [1, 16]):
    g = ss[ss.threads == T].groupby("pf_scatter")["scatter"]
    ax.errorbar(g.median().index, g.median().values, yerr=g.std().values,
                marker="s", color="#1565C0", lw=1.8, markersize=6, capsize=2.5)
    ax.axvline(12, color="#2E7D32", ls="--", lw=1.4)
    ax.text(12.6, g.median().max() * 0.92, "distanza scelta: 12", color="#2E7D32", fontsize=9.5)
    ax.set_xlabel("distanza di prefetch (iterazioni)")
    ax.set_title(f"T = {T}", fontsize=11)
    ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True); ax.set_ylim(bottom=0)
axes[0].set_ylabel("tempo scatter R+S (ms)")
fig.suptitle("Distanza di prefetch nello scatter (0 = disattivato; uniforme)", fontsize=12.5)
fig.tight_layout(rect=[0, 0, 1, 0.92])
fig.savefig(os.path.join(OUT, "pf_scatter_sweep.png"), dpi=170)
plt.close(fig)

ps = pd.read_csv(os.path.join(RES, "pf_probe_sweep.csv"))
fig, axes = plt.subplots(1, 2, figsize=(11, 4.3))
for ax, T in zip(axes, [1, 16]):
    g = ps[ps.threads == T].groupby("pf_probe")["join_ms"]
    ax.errorbar(g.median().index, g.median().values, yerr=g.std().values,
                marker="s", color="#7B1FA2", lw=1.8, markersize=6, capsize=2.5)
    ax.axvline(8, color="#2E7D32", ls="--", lw=1.4)
    ax.text(8.6, g.median().max() * 0.92, "distanza scelta: 8", color="#2E7D32", fontsize=9.5)
    ax.set_xlabel("distanza di prefetch (iterazioni)")
    ax.set_title(f"T = {T}", fontsize=11)
    ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True); ax.set_ylim(bottom=0)
axes[0].set_ylabel("tempo della fase join (ms)")
fig.suptitle("Distanza di prefetch nel probe (0 = disattivato; uniforme)", fontsize=12.5)
fig.tight_layout(rect=[0, 0, 1, 0.92])
fig.savefig(os.path.join(OUT, "pf_probe_sweep.png"), dpi=170)
print("ok ->", OUT)
