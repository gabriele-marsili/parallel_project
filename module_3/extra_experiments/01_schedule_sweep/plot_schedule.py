#!/usr/bin/env python3
"""Esp.1 — sensibilità alla schedule del join oltre T=8.
Figura 1: tempo del join per schedule e T, uniforme vs skew.
Figura 2: granularità fine, static vs dynamic1 al crescere di P."""
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

df = pd.read_csv(os.path.join(RES, "schedule_sweep.csv"))

STYLE = {"static":    ("#D32F2F", "o"),
         "static1":   ("#E57373", "v"),
         "guided1":   ("#EF6C00", "P"),
         "guided16":  ("#FFB74D", "X"),
         "dynamic1":  ("#1565C0", "s"),
         "dynamic4":  ("#64B5F6", "^"),
         "dynamic16": ("#0D47A1", "D")}
ORDER = ["static", "static1", "guided1", "guided16", "dynamic1", "dynamic4", "dynamic16"]
TICKS = sorted(df["threads"].unique())

fig, axes = plt.subplots(1, 2, figsize=(11, 4.4), sharex=True)
for ax, wl, title in [(axes[0], "uniform", "carico uniforme"),
                      (axes[1], "skew", "carico skewed (rho=0.9, 4 hot)")]:
    sub = df[df.workload == wl]
    for s in ORDER:
        g = sub[sub.sched == s].groupby("threads")["join_ms"]
        med, std = g.median(), g.std()
        c, mk = STYLE[s]
        ax.errorbar(med.index, med.values, yerr=std.values, marker=mk, color=c,
                    label=s.replace("dynamic", "dynamic,").replace("static1", "static,1")
                          .replace("guided", "guided,"),
                    lw=1.8, markersize=6, capsize=2.5)
    ax.set_xlabel("numero di thread")
    ax.set_title(title, fontsize=11)
    ax.set_xscale("log", base=2); ax.set_xticks(TICKS); ax.set_xticklabels(TICKS)
    ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
axes[0].set_ylabel("tempo della fase join (ms)")
axes[0].legend(fontsize=8.5, ncol=2)
fig.suptitle("Schedule del join su tutto il range di thread (P=128)", fontsize=12.5)
fig.tight_layout(rect=[0, 0, 1, 0.94])
fig.savefig(os.path.join(OUT, "schedule_sweep.png"), dpi=170)
plt.close(fig)

gr = pd.read_csv(os.path.join(RES, "schedule_granularity.csv"))
fig, axes = plt.subplots(1, 2, figsize=(11, 4.2))
for s, c, mk in [("static", "#D32F2F", "o"), ("dynamic1", "#1565C0", "s")]:
    g = gr[gr.sched == s].groupby("P")["join_ms"]
    axes[0].errorbar(g.median().index, g.median().values, yerr=g.std().values,
                     marker=mk, color=c, lw=1.8, markersize=6, capsize=2.5,
                     label="dynamic,1" if s == "dynamic1" else s)
axes[0].set_xscale("log", base=2)
axes[0].set_xlabel("numero di partizioni P"); axes[0].set_ylabel("tempo della fase join (ms)")
axes[0].set_title("join: static vs dynamic,1 (T=16, uniforme)", fontsize=11)
axes[0].grid(ls=":", alpha=0.45); axes[0].set_axisbelow(True); axes[0].legend(fontsize=9.5)
axes[0].set_ylim(bottom=0)

diff = gr.pivot_table(index=["P", "rep"], columns="sched", values="join_ms").reset_index()
d = diff["dynamic1"] - diff["static"]
gap_med = d.groupby(diff["P"]).median()
gap_lo = d.groupby(diff["P"]).min()
gap_hi = d.groupby(diff["P"]).max()
join_med = gr[gr.sched == "static"].groupby("P")["join_ms"].median()
xs = [str(p) for p in gap_med.index]
yerr = [(gap_med - gap_lo).values, (gap_hi - gap_med).values]
# blu quando tutte le rep hanno lo stesso segno (differenza reale), grigio quando
# l'intervallo min-max attraversa lo zero (indistinguibile dal rumore)
colors = ["#1565C0" if lo * hi > 0 else "#9E9E9E"
          for lo, hi in zip(gap_lo.values, gap_hi.values)]
axes[1].bar(xs, gap_med.values, color=colors, width=0.55, yerr=yerr, capsize=4)
axes[1].axhline(0, color="black", lw=0.8)
axes[1].set_ylim(gap_lo.min() - 0.35, gap_hi.max() + 0.45)
for i, p in enumerate(gap_med.index):
    pct = 100.0 * gap_med[p] / join_med[p]
    va = "bottom" if gap_med[p] >= 0 else "top"
    y = gap_hi[p] + 0.07 if gap_med[p] >= 0 else gap_lo[p] - 0.07
    axes[1].text(i, y, f"{pct:+.1f}%", ha="center", va=va, fontsize=9)
axes[1].annotate("qui dynamic,1 e' piu lento", xy=(0.98, 0.96), xycoords="axes fraction",
                 fontsize=9, color="#555555", va="top", ha="right")
axes[1].annotate("qui dynamic,1 e' piu veloce", xy=(0.98, 0.04), xycoords="axes fraction",
                 fontsize=9, color="#555555", va="bottom", ha="right")
axes[1].set_xlabel("numero di partizioni P")
axes[1].set_ylabel("join dynamic,1 - join static (ms)")
axes[1].set_title("differenza dei tempi (mediana e min-max su 5 rep)\n"
                  "blu = segno uguale in tutte le rep, grigio = rumore",
                  fontsize=10)
axes[1].grid(ls=":", alpha=0.45, axis="y"); axes[1].set_axisbelow(True)
fig.suptitle("Granularita del join: P alto = iterazioni piccole (T=16, uniforme)", fontsize=12.5)
fig.tight_layout(rect=[0, 0, 1, 0.93])
fig.savefig(os.path.join(OUT, "schedule_granularity.png"), dpi=170)
print("ok ->", OUT)
