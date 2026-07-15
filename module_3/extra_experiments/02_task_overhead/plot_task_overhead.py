#!/usr/bin/env python3
"""Esp.2 — costo del modello a task sulle fasi regolari.
Figura 1: histogram e scatter vs numero di task per fase, con il loop come riferimento.
Figura 2: nowait vs senza nowait sul single del join task."""
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

tc = pd.read_csv(os.path.join(RES, "task_chunks.csv"))
tc["hist"] = tc.hist_r_ms + tc.hist_s_ms
tc["scatter"] = tc.scatter_r_ms + tc.scatter_s_ms

task = tc[tc["mode"] == "task"]
loop = tc[tc["mode"] == "loop"]

fig, ax = plt.subplots(figsize=(8.6, 4.6))
for col, c, mk, lbl in [("hist", "#7B1FA2", "s", "histogram R+S (task)"),
                        ("scatter", "#9C27B0", "^", "scatter R+S (task)")]:
    g = task.groupby("tchunks")[col]
    ax.errorbar(g.median().index, g.median().values, yerr=g.std().values,
                marker=mk, color=c, lw=1.8, markersize=6, capsize=2.5, label=lbl)
for col, c, ls, lbl in [("hist", "#1565C0", "--", "histogram R+S (loop, riferimento)"),
                        ("scatter", "#64B5F6", ":", "scatter R+S (loop, riferimento)")]:
    ax.axhline(loop[col].median(), color=c, ls=ls, lw=1.8, label=lbl)
ax.set_xscale("log", base=2)
ax.set_xticks(sorted(task.tchunks.unique()))
ax.set_xticklabels(sorted(task.tchunks.unique()))
ax.set_xlabel("numero di task per fase")
ax.set_ylabel("tempo di fase (ms)")
ax.set_title("Fasi regolari a task: il grosso del gap e' la granularita grossa, non il dispatch\n(T=16, uniforme; minimo a 64/512 task, lieve risalita oltre = costo per-task ~1 microsecondo)", fontsize=11)
ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True); ax.legend(fontsize=9)
ax.set_ylim(bottom=0)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "task_chunks.png"), dpi=170)
plt.close(fig)

nw = pd.read_csv(os.path.join(RES, "nowait.csv"))
fig, axes = plt.subplots(1, 2, figsize=(11, 4.2), sharey=False)
for ax, wl, title in [(axes[0], "uniform", "carico uniforme"),
                      (axes[1], "skew", "carico skewed")]:
    sub = nw[nw.workload == wl]
    ts = sorted(sub.threads.unique())
    x = np.arange(len(ts)); w = 0.36
    for off, mode, c, lbl in [(-w/2, "task", "#7B1FA2", "con nowait (consegnato)"),
                              (w/2, "task_nonowait", "#9E9E9E", "senza nowait")]:
        g = sub[sub["mode"] == mode].groupby("threads")["total_ms"]
        ax.bar(x + off, g.median().reindex(ts).values, width=w, color=c, label=lbl,
               yerr=g.std().reindex(ts).values, capsize=3)
    ax.set_xticks(x); ax.set_xticklabels(ts)
    ax.set_xlabel("numero di thread"); ax.set_title(title, fontsize=11)
    ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True)
axes[0].set_ylabel("tempo totale (ms)")
axes[0].legend(fontsize=9.5)
fig.suptitle("Variante task con e senza nowait sul single: differenza dentro il rumore", fontsize=12.5)
fig.tight_layout(rect=[0, 0, 1, 0.93])
fig.savefig(os.path.join(OUT, "nowait.png"), dpi=170)
print("ok ->", OUT)
