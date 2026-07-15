#!/usr/bin/env python3
"""Esp.1 — baseline di partenza vs versione consegnata. Legge results/, scrive plots/."""
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

C = {"hist": "#42A5F5", "scat": "#1E88E5", "join": "#EF5350"}

# ── Fig 1: 4 varianti, tempo per fase impilato ──
df = pd.read_csv(os.path.join(RES, "ablation_p128.csv"))
df["k"] = df["hash"] + "/" + df["map"]
order = ["mod/umap", "fib/umap", "mod/flat", "fib/flat"]
lab = {"mod/umap": "V0 baseline di partenza\n(mod + unordered_map)", "fib/umap": "V1\n(+ hash fib)",
       "mod/flat": "V2\n(+ FlatCountMap)", "fib/flat": "V3\n(fib + FlatCountMap)"}
df = df.set_index("k").reindex(order).reset_index()

fig, ax = plt.subplots(figsize=(9.5, 5.6))
x = np.arange(len(order)); w = 0.55
hist = df["hist_ms"].values; scat = df["scatter_ms"].values; join = df["join_ms"].values
ax.bar(x, hist, w, label="Histogram+prefix (R+S)", color=C["hist"], edgecolor="white", zorder=3)
ax.bar(x, scat, w, bottom=hist, label="Scatter (R+S)", color=C["scat"], edgecolor="white", zorder=3)
ax.bar(x, join, w, bottom=hist+scat, label="Join local (build+probe)", color=C["join"], edgecolor="white", zorder=3)
tot = hist + scat + join
for xi, t in zip(x, tot):
    ax.text(xi, t + 8, f"{t:.0f} ms", ha="center", va="bottom", fontsize=10, weight="bold")
ax.text(1.5, max(tot) * 1.18, f"V0 -> V3 = {tot[0]/tot[3]:.2f}x piu' veloce end-to-end",
        ha="center", color="#2E7D32", weight="bold", fontsize=11)
ax.set_xticks(x); ax.set_xticklabels([lab[o] for o in order], fontsize=9.5)
ax.set_ylabel("tempo sequenziale (ms). NR=10M, NS=20M, P=128, max_key=1M")
ax.set_ylim(0, max(tot) * 1.28)
ax.set_title("Contributo isolato di ogni modifica rispetto alla versione di partenza (node Ivy Bridge)")
ax.legend(loc="upper right", fontsize=9.5, framealpha=0.95)
ax.grid(axis="y", ls=":", alpha=0.4, zorder=0); ax.set_axisbelow(True)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "ablation_variants.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

# ── Fig 2: sweep densita chiavi, umap vs flat (join phase) ──
dd = pd.read_csv(os.path.join(RES, "ablation_density.csv"))
fig, ax = plt.subplots(figsize=(9.5, 5.2))
for impl, col, mk, nm in [("umap", "#D32F2F", "o", "unordered_map (baseline di partenza)"),
                          ("flat", "#2E7D32", "s", "FlatCountMap")]:
    g = dd[dd["map"] == impl].sort_values("max_key")
    ax.plot(g["max_key"], g["join_ms"], mk + "-", color=col, label=nm, markersize=8, lw=2.2)
    for xx, yy in zip(g["max_key"], g["join_ms"]):
        ax.annotate(f"{yy:.0f}", (xx, yy), textcoords="offset points", xytext=(0, 8),
                    ha="center", fontsize=7.5, color=col)
ax.set_xscale("log")
ax.set_xlabel("max_key  (dominio chiavi; grande = chiavi quasi uniche = tabelle piu' grandi)")
ax.set_ylabel("tempo fase Join local (ms)")
ax.set_title("Vantaggio della FlatCountMap al crescere delle chiavi distinte (hash fissa: fib)")
ax.legend(fontsize=10); ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "ablation_density.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

# stampa i numeri chiave per il companion
v = df.set_index("k")
print("=== Esp.1 numeri ===")
for o in order:
    r = v.loc[o]; print(f"  {o:9s} tot={r['total_ms']:.0f}ms (hist {r['hist_ms']:.0f} scat {r['scatter_ms']:.0f} join {r['join_ms']:.0f})")
print(f"  V0/V3 total speedup = {v.loc['mod/umap','total_ms']/v.loc['fib/flat','total_ms']:.2f}x")
print(f"  join umap->flat (mod fixed): {v.loc['mod/umap','join_ms']/v.loc['mod/flat','join_ms']:.2f}x")
print("OK -> plots/ablation_variants.png, plots/ablation_density.png")
