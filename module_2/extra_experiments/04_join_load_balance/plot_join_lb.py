#!/usr/bin/env python3
"""Esp.4 — distribuzione del carico nella fase join: cyclic/block/dynamic/lpt.
Con barre d'errore (mean +/- std su 15 run) per mostrare che le differenze sono nel rumore."""
import os
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results"); OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

STRAT = ["cyclic", "block", "dynamic", "lpt"]
COL = {"cyclic": "#2E7D32", "block": "#D32F2F", "dynamic": "#1565C0", "lpt": "#7B1FA2"}
LAB = {"cyclic": "cyclic", "block": "block", "dynamic": "dynamic", "lpt": "LPT"}

df = pd.read_csv(os.path.join(RES, "join_lb.csv"))
uni = df[df["skew"] == 0.0].set_index("strategy").reindex(STRAT)
skw = df[df["skew"] > 0.0].set_index("strategy").reindex(STRAT)

# ── Fig 1: wall time con barre d'errore, uniforme vs skew ──
fig, axes = plt.subplots(1, 2, figsize=(13, 5.6))
for ax, data, title in [
    (axes[0], uni, "carico uniforme: le 4 strategie coincidono entro il rumore (barre = +/- std, 15 run)"),
    (axes[1], skw, "carico skewed (0.9, hot=4): solo block collassa; le altre 3 pari, al pavimento")]:
    x = np.arange(len(STRAT))
    ax.bar(x, data["wall_mean_ms"].values, 0.6, yerr=data["wall_std_ms"].values, capsize=6,
           color=[COL[s] for s in STRAT], edgecolor="black", lw=0.4, zorder=3,
           error_kw=dict(ecolor="black", lw=1.4))
    for xi, s in zip(x, STRAT):
        w = data.loc[s, "wall_mean_ms"]; sd = data.loc[s, "wall_std_ms"]; imb = data.loc[s, "imbalance"]
        ax.text(xi, w + sd + max(data["wall_mean_ms"])*0.02, f"{w:.0f}±{sd:.0f} ms\nimb {imb:.2f}",
                ha="center", va="bottom", fontsize=8.5, weight="bold")
    ax.set_xticks(x); ax.set_xticklabels([LAB[s] for s in STRAT], fontsize=9.5)
    ax.set_title(title, fontsize=10)
    ax.set_ylabel("tempo fase Join (ms)")
    ax.set_ylim(0, max(data["wall_mean_ms"] + data["wall_std_ms"]) * 1.32)
    ax.grid(axis="y", ls=":", alpha=0.4); ax.set_axisbelow(True)
mpf = skw["max_part_frac"].iloc[0]
axes[1].text(1.5, max(skw["wall_mean_ms"])*1.16,
             f"pavimento teorico {mpf:.2f}x (la partizione piu' calda, non divisibile)",
             fontsize=8.5, color="#7f0000", ha="center")
fig.suptitle("Distribuzione del carico nella fase Join (NR=10M, P=128, t=16, node Ivy Bridge)",
             fontsize=12, y=1.02)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "join_lb_uniform_vs_skew.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

# ── Fig 2: imbalance vs skew ──
sw = pd.read_csv(os.path.join(RES, "join_lb_sweep.csv"))
fig, ax = plt.subplots(figsize=(9.5, 5.4))
for s in ["cyclic", "dynamic", "lpt"]:
    g = sw[sw["strategy"] == s].sort_values("skew")
    ax.plot(g["skew"], g["imbalance"], "o-", color=COL[s], label=LAB[s].replace("\n", " "), markersize=7, lw=2.2)
g0 = sw[sw["strategy"] == "cyclic"].sort_values("skew")
ax.plot(g0["skew"], g0["max_part_frac"], "k--", lw=1.4, alpha=0.7, label="pavimento (max_part_frac)")
ax.axhline(1.0, ls=":", color="#888", lw=1)
ax.set_xlabel("skew (frazione di chiavi sulle 4 partizioni calde)")
ax.set_ylabel("imbalance = max(busy) / mean(busy)")
ax.set_title("Sotto skew cyclic ha imbalance un filo sopra LPT e dynamic, tutti vicini al pavimento")
ax.legend(fontsize=10); ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "join_lb_skew_sweep.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

print("=== Esp.4 numeri ===")
for lab, data in [("UNIFORME", uni), ("SKEW", skw)]:
    print(f"-- {lab} --")
    for s in STRAT:
        print(f"  {s:8s} {data.loc[s,'wall_mean_ms']:6.1f} +/- {data.loc[s,'wall_std_ms']:.1f} ms  imb {data.loc[s,'imbalance']:.2f}  sched {data.loc[s,'sched_ms']*1000:.1f} us")
print(f"  max_part_frac skew = {mpf:.2f}")
# range uniforme vs std: prova che le differenze sono nel rumore
umin, umax = uni["wall_mean_ms"].min(), uni["wall_mean_ms"].max()
ustd = uni["wall_std_ms"].max()
print(f"  UNIFORME: spread mean {umax-umin:.1f} ms vs std max {ustd:.1f} ms -> {'DENTRO' if (umax-umin)<2*ustd else 'FUORI'} il rumore")
print("OK -> join_lb_uniform_vs_skew.png, join_lb_skew_sweep.png")
