#!/usr/bin/env python3
"""Esp.2 — costo del modello a task.
Figura 1: tempo totale scomposto in fasi regolari e join, loop vs task.
Figura 2: fasi regolari vs numero di task per thread, normalizzate sul loop.
Figura 3: nowait vs senza nowait sul single del join task."""
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.environ.get("ESP2_RES", os.path.join(HERE, "results"))
OUT = os.environ.get("ESP2_OUT", os.path.join(HERE, "plots"))
os.makedirs(OUT, exist_ok=True)

# la tinta è il modello (rosso task, blu loop), l'intensità è la fase
TASK_D, TASK_L = "#A31515", "#F2836B"
LOOP_D, LOOP_L = "#0B3D91", "#4FC3F7"
# per il sweep le curve sono tutte task: l'intensità del rosso è il numero di thread
T_COLOR = {8: "#F2836B", 16: "#D93A1E", 32: "#6E0D0D"}
WL_TITLE = {"uniform": "carico uniforme", "skew": "carico skewed (rho=0.9, 4 partizioni hot)"}
TS = [8, 16, 32]

tc = pd.read_csv(os.path.join(RES, "task_chunks.csv"))
nw = pd.read_csv(os.path.join(RES, "nowait.csv"))
for d in (tc, nw):
    d["hist"] = d.hist_r_ms + d.hist_s_ms
    d["scatter"] = d.scatter_r_ms + d.scatter_s_ms
    d["reg"] = d["hist"] + d["scatter"]
    d["jn"] = d.join_ms

# ---------------------------------------------------------------- figura 1
# granularità consegnata: il loop viene da task_chunks.csv, il task da nowait.csv
loop1 = tc[tc["mode"] == "loop"]
task1 = nw[nw["mode"] == "task"]


def agg(df, wl, T):
    s = df[(df.workload == wl) & (df.threads == T)]
    tot = s.reg + s.jn
    return s.reg.median(), s.jn.median(), tot.median(), tot.min(), tot.max()


fig, axes = plt.subplots(1, 2, figsize=(12.4, 5.4))
for ax, wl in zip(axes, ("uniform", "skew")):
    x = np.arange(len(TS))
    w = 0.38
    for off, df, c_reg, c_jn, name in [(-w / 2, loop1, "#9DC3E6", LOOP_D, "loop"),
                                       (w / 2, task1, "#F2B8A2", TASK_D, "task")]:
        a = [agg(df, wl, T) for T in TS]
        rg = [v[0] for v in a]
        jn = [v[1] for v in a]
        tt = np.array([v[2] for v in a])
        lo = np.array([v[3] for v in a])
        hi = np.array([v[4] for v in a])
        ax.bar(x + off, rg, w, color=c_reg, edgecolor="white", linewidth=0.6,
               label=f"fasi regolari (histogram+scatter) - {name}")
        ax.bar(x + off, jn, w, bottom=rg, color=c_jn, edgecolor="white", linewidth=0.6,
               label=f"join - {name}")
        ax.errorbar(x + off, tt, yerr=[tt - lo, hi - tt], fmt="none", ecolor="#333",
                    elinewidth=1.1, capsize=3, alpha=0.7)
    for k, T in enumerate(TS):
        lt, tv = agg(loop1, wl, T)[2], agg(task1, wl, T)[2]
        g = (tv / lt - 1) * 100
        yt = max(agg(loop1, wl, T)[4], agg(task1, wl, T)[4])
        ax.text(k, yt + 3.5, f"{g:+.0f}%", ha="center", va="bottom", fontweight="bold",
                color=TASK_D if g > 0 else "#127A2E", fontsize=12)
    ax.set_xticks(x)
    ax.set_xticklabels([f"T={t}" for t in TS])
    ax.set_title(WL_TITLE[wl], fontsize=12, pad=20)
    ax.grid(ls=":", alpha=0.4, axis="y")
    ax.set_axisbelow(True)
    ax.set_ylim(0, 140)
    ax.set_ylabel("tempo totale (ms) - mediana su 5 ripetizioni")
axes[0].legend(fontsize=8.4, loc="upper right", framealpha=0.95)
fig.suptitle("Scomposizione del tempo totale in fasi regolari e join: variante loop e variante "
             "task (un task per thread)\nP=128, NR=10M, NS=20M; mediana su 5 ripetizioni, "
             "barre = intervallo min-max", fontsize=11.5, y=1.02)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "loop_vs_task_phases.png"), dpi=170, bbox_inches="tight")
plt.close(fig)

# ---------------------------------------------------------------- figura 2
# Asse x normalizzato: task per thread = tchunks/T. Così i tre T condividono la
# soglia di sotto-utilizzo (< 1 task per thread) su una sola verticale.
# Asse y normalizzato: rapporto sul loop allo stesso T e carico, che è il
# riferimento corretto. Senza normalizzare servirebbero sei scale diverse.
task2 = tc[tc["mode"] == "task"]
loop2 = tc[tc["mode"] == "loop"]
PHASES = [("hist", "histogram R+S"), ("scatter", "scatter R+S")]

fig, axes = plt.subplots(2, 2, figsize=(11.6, 8.0), sharex=True, squeeze=False)
for i, (col, phase_lbl) in enumerate(PHASES):
    for j, wl in enumerate(("uniform", "skew")):
        ax = axes[i][j]
        ax.axvspan(0.35, 1.0, color="#BBBBBB", alpha=0.28, zorder=0, lw=0)
        ax.axhline(1.0, color=LOOP_D, ls="--", lw=2.0, zorder=2,
                   label="riferimento #pragma omp for (= 1.0)")
        for T in TS:
            ref = loop2[(loop2.threads == T) & (loop2.workload == wl)][col].median()
            s = task2[(task2.threads == T) & (task2.workload == wl)]
            g = s.groupby("tchunks")[col]
            xs = np.array(sorted(s.tchunks.unique())) / T
            med = g.median().values / ref
            lo = g.min().values / ref
            hi = g.max().values / ref
            ax.fill_between(xs, lo, hi, color=T_COLOR[T], alpha=0.16, lw=0, zorder=3)
            ax.plot(xs, med, marker="o", color=T_COLOR[T], lw=2.1, markersize=5.5,
                    zorder=4, label=f"task, T={T}")
        ax.set_xscale("log", base=2)
        ax.set_xticks([0.5, 1, 2, 4, 8, 16, 32, 64, 128])
        ax.set_xticklabels(["0.5", "1", "2", "4", "8", "16", "32", "64", "128"], fontsize=9)
        ax.set_title(f"{phase_lbl} - {WL_TITLE[wl]}", fontsize=10.5)
        ax.grid(ls=":", alpha=0.45, zorder=1)
        ax.set_axisbelow(True)
    lim = 3.6 if col == "hist" else 2.25
    for j in range(2):
        axes[i][j].set_ylim(0.8, lim)
    axes[i][0].set_ylabel(f"{phase_lbl}: tempo task / tempo loop", fontsize=10)

axes[0][0].text(0.62, 3.35, "meno di un task\nper thread", fontsize=8.5, ha="center",
                va="top", color="#444", style="italic")
axes[1][0].annotate("T=32, 16 task:\nmeta' dei thread inattiva,\nla fase raddoppia (x2.0)",
                    xy=(0.5, 2.0), xytext=(1.6, 1.95), fontsize=8.5, color="#444",
                    arrowprops=dict(arrowstyle="->", color="#666", lw=1.1))
# la risalita a destra e' l'unico punto dove il costo di creazione affiora, ma la
# dispersione fra ripetizioni la' vale il 29-33%: va segnalata, non affermata
axes[0][1].annotate("T=32, 1024 task:\nrisalita, ma dispersione\ndel 33% fra le ripetizioni",
                    xy=(32, 3.31), xytext=(3.2, 3.15), fontsize=8.5, color="#444",
                    arrowprops=dict(arrowstyle="->", color="#666", lw=1.1))
for j in range(2):
    axes[-1][j].set_xlabel("task per thread (numero di task per fase / T)")

h, l = axes[0][0].get_legend_handles_labels()
fig.legend(h, l, fontsize=9.5, ncol=4, columnspacing=1.6, handlelength=2.4,
           loc="lower center", bbox_to_anchor=(0.5, -0.004), frameon=False)
fig.suptitle("Fasi regolari della variante task al variare della granularita', normalizzate sul "
             "loop allo stesso T e carico (P=128)\nsotto 1 task per thread parte dei thread "
             "resta inattiva; sopra, la curva e' piatta ma resta stabilmente sopra il loop",
             fontsize=11, y=0.985)
fig.tight_layout(rect=[0, 0.045, 1, 0.945])
fig.savefig(os.path.join(OUT, "task_chunks.png"), dpi=170)
plt.close(fig)

# ---------------------------------------------------------------- figura 3
fig, axes = plt.subplots(1, 2, figsize=(11, 4.2), sharey=False)
for ax, wl in zip(axes, ("uniform", "skew")):
    sub = nw[nw.workload == wl]
    ts = sorted(sub.threads.unique())
    x = np.arange(len(ts))
    w = 0.36
    for off, mode, c, lbl in [(-w / 2, "task", TASK_D, "con nowait (consegnato)"),
                              (w / 2, "task_nonowait", TASK_L, "senza nowait")]:
        g = sub[sub["mode"] == mode].groupby("threads")["total_ms"]
        ax.bar(x + off, g.median().reindex(ts).values, width=w, color=c, label=lbl,
               yerr=g.std().reindex(ts).values, capsize=3)
    ax.set_xticks(x)
    ax.set_xticklabels(ts)
    ax.set_xlabel("numero di thread")
    ax.set_title(WL_TITLE[wl], fontsize=11)
    ax.grid(ls=":", alpha=0.45, axis="y")
    ax.set_axisbelow(True)
axes[0].set_ylabel("tempo totale (ms)")
axes[0].legend(fontsize=9.5)
fig.suptitle("Variante task con e senza nowait sul single: differenza entro il 3% su ogni T e "
             "carico\ndopo il single non c'e' lavoro indipendente, cambia solo quale barriera "
             "assorbe i worker", fontsize=11.5)
fig.tight_layout(rect=[0, 0, 1, 0.89])
fig.savefig(os.path.join(OUT, "nowait.png"), dpi=170)
plt.close(fig)
print("ok ->", OUT)
