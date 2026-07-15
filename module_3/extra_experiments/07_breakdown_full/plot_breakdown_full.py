#!/usr/bin/env python3
"""Esp.7 — breakdown per fase su tutto il range di thread, ricostruito dai
CSV del report (results/cluster/strong_scaling.csv, medie di 3 ripetizioni).
Figura 1: range completo T=1..32, incluso il T=1 che la fig. 4 del report
omette: si vede perché domina visivamente.
Figura 2: solo il regime T=16..32, per leggere cosa succede oltre la fig. 4."""
import os
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

CSV = os.path.join(HERE, "..", "..", "results", "cluster", "strong_scaling.csv")
df = pd.read_csv(CSV)
df["histogram"] = (df.t_hist_r + df.t_hist_s) * 1000
df["scatter"] = (df.t_scatter_r + df.t_scatter_s) * 1000
df["join"] = df.t_join * 1000

PHASES = [("histogram", "#64B5F6"), ("scatter", "#EF6C00"), ("join", "#7B1FA2")]


def draw(ts, fname, title):
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.8), sharey=True)
    for ax, wl, sub_title in [(axes[0], "uniform", "carico uniforme"),
                              (axes[1], "skewed", "carico skewed")]:
        sub = df[(df.workload == wl) & (df.threads.isin(ts))]
        x = np.arange(len(ts)); w = 0.36
        for off, impl, hatch in [(-w / 2, "loop", None), (w / 2, "task", "//")]:
            g = sub[sub.impl == impl].groupby("threads")[["histogram", "scatter", "join"]].mean().reindex(ts)
            bottom = np.zeros(len(ts))
            for ph, c in PHASES:
                vals = g[ph].values
                ax.bar(x + off, vals, width=w, bottom=bottom, color=c, hatch=hatch,
                       edgecolor="white", linewidth=0.4,
                       label=f"{ph}" if impl == "loop" else None)
                bottom += vals
        ax.set_xticks(x); ax.set_xticklabels(ts)
        ax.set_xlabel("numero di thread")
        ax.set_title(sub_title, fontsize=11)
        ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True)
    axes[0].set_ylabel("tempo di fase (ms)")
    handles, labels = axes[0].get_legend_handles_labels()
    import matplotlib.patches as mpatches
    handles += [mpatches.Patch(facecolor="#BDBDBD", label="loop (barra piena)"),
                mpatches.Patch(facecolor="#BDBDBD", hatch="//", label="task (barra tratteggiata)")]
    axes[0].legend(handles=handles, fontsize=8.5)
    fig.suptitle(title, fontsize=12.5)
    fig.tight_layout(rect=[0, 0, 1, 0.92])
    fig.savefig(os.path.join(OUT, fname), dpi=170)
    plt.close(fig)


draw([1, 2, 4, 8, 16, 20, 32], "breakdown_full.png",
     "Breakdown per fase su tutto il range (dai dati del report, medie di 3 rep)")
draw([16, 20, 32], "breakdown_smt.png",
     "Breakdown per fase nel regime di saturazione e SMT (T=16, 20, 32)")
print("ok ->", OUT)
