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

# una scala di colore per famiglia: la tinta dice la famiglia (rosso static,
# ambra guided, blu dynamic), l'intensità dice il chunk (scuro = chunk 1, chiaro =
# chunk grande). Un solo marker per famiglia. I gradini sono scelti in modo che ogni
# coppia resti separata sia a visione normale sia con daltonismo; il bordo scuro sul
# marker tiene leggibili i toni chiari su fondo bianco.
STATIC_D, STATIC_L = "#A31515", "#F2836B"
GUIDED_D, GUIDED_L = "#B36A00", "#FFCE54"
DYN_D, DYN_M, DYN_L = "#0B3D91", "#2E86DE", "#4FC3F7"
STATIC, GUIDED, DYNAMIC = STATIC_L, GUIDED_D, DYN_D
STYLE = {"static":    (STATIC_L, STATIC_D, "o", "static"),
         "static1":   (STATIC_D, STATIC_D, "o", "static,1"),
         "guided1":   (GUIDED_D, GUIDED_D, "s", "guided,1"),
         "guided16":  (GUIDED_L, GUIDED_D, "s", "guided,16"),
         "dynamic1":  (DYN_D,    DYN_D,    "^", "dynamic,1"),
         "dynamic4":  (DYN_M,    DYN_D,    "^", "dynamic,4"),
         "dynamic16": (DYN_L,    DYN_D,    "^", "dynamic,16")}
ORDER = ["static", "static1", "guided1", "guided16", "dynamic1", "dynamic4", "dynamic16"]
TICKS = sorted(df["threads"].unique())

fig, axes = plt.subplots(1, 2, figsize=(11, 4.9), sharex=True)
for ax, wl, title in [(axes[0], "uniform", "carico uniforme"),
                      (axes[1], "skew", "carico skewed (rho=0.9, 4 hot)")]:
    sub = df[df.workload == wl]
    for s in ORDER:
        g = sub[sub.sched == s].groupby("threads")["join_ms"]
        med, std = g.median(), g.std()
        c, edge, mk, lab = STYLE[s]
        ax.errorbar(med.index, med.values, yerr=std.values, marker=mk, color=c,
                    label=lab, lw=2.2, markersize=7, capsize=2.5,
                    markeredgecolor=edge, markeredgewidth=0.9)
    ax.set_xlabel("numero di thread")
    ax.set_title(title, fontsize=11)
    ax.set_xscale("log", base=2); ax.set_xticks(TICKS); ax.set_xticklabels(TICKS)
    ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
axes[0].set_ylabel("tempo della fase join (ms)")
h, l = axes[0].get_legend_handles_labels()
fig.legend(h, l, fontsize=9.5, ncol=7, columnspacing=1.2, handlelength=2.8,
           loc="lower center", bbox_to_anchor=(0.5, -0.01), frameon=False)
fig.suptitle("Clausola schedule di #pragma omp for sul join, tutto il range di thread "
             "(P=128, assegnamento LPT)\n"
             "colore: la tinta è la famiglia di schedule, l'intensità è il chunk (scuro = 1, chiaro = grande)",
             fontsize=12.5, y=0.99)
fig.tight_layout(rect=[0, 0.06, 1, 0.92])
fig.savefig(os.path.join(OUT, "schedule_sweep.png"), dpi=170)
plt.close(fig)

gr = pd.read_csv(os.path.join(RES, "schedule_granularity.csv"))
WL_TITLE = {"uniform": "carico uniforme", "skew": "carico skewed (rho=0.9, 4 hot)"}
WL_SHORT = {"uniform": "uniforme", "skew": "skewed"}
WL_COLOR = {"uniform": "#0072B2", "skew": "#E69F00"}
# i workload presenti dipendono dalla run: la parte 2 di run.sh ha girato solo su
# uniform fino al rerun che aggiunge skew
WLS = [w for w in ("uniform", "skew") if w in set(gr.workload)]

fig, axes = plt.subplots(1, len(WLS) + 1, figsize=(4.6 * (len(WLS) + 1), 4.6))

for ax, wl in zip(axes, WLS):
    sub = gr[gr.workload == wl]
    for s, c, edge, mk in [("static", STATIC_L, STATIC_D, "o"), ("dynamic1", DYN_D, DYN_D, "^")]:
        g = sub[sub.sched == s].groupby("P")["join_ms"]
        ax.errorbar(g.median().index, g.median().values, yerr=g.std().values,
                    marker=mk, color=c, lw=2.2, markersize=7, capsize=2.5,
                    markeredgecolor=edge, markeredgewidth=0.9,
                    label="dynamic,1" if s == "dynamic1" else s)
    ax.set_xscale("log", base=2)
    ax.set_xlabel("numero di partizioni P")
    ax.set_title(WL_TITLE[wl], fontsize=11)
    ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True); ax.legend(fontsize=9.5)
    ax.set_ylim(bottom=0)
axes[0].set_ylabel("tempo della fase join (ms)")

# pannello di destra: gap relativo, cosi' i due workload stanno sulla stessa scala
# nonostante skew impieghi il triplo del tempo
gap = axes[-1]
span = {}
Ps = sorted(gr.P.unique())
x = np.arange(len(Ps))
width = 0.8 / len(WLS)
for k, wl in enumerate(WLS):
    sub = gr[gr.workload == wl]
    piv = sub.pivot_table(index=["P", "rep"], columns="sched", values="join_ms").reset_index()
    base = sub[sub.sched == "static"].groupby("P")["join_ms"].median()
    rel = 100.0 * (piv["dynamic1"] - piv["static"]) / piv["P"].map(base)
    med = rel.groupby(piv["P"]).median().reindex(Ps)
    lo = rel.groupby(piv["P"]).min().reindex(Ps)
    hi = rel.groupby(piv["P"]).max().reindex(Ps)
    # grigio dove l'intervallo min-max attraversa lo zero: differenza indistinguibile dal rumore
    cols = [WL_COLOR[wl] if l * h > 0 else "#9E9E9E" for l, h in zip(lo.values, hi.values)]
    pos = x + (k - (len(WLS) - 1) / 2) * width
    gap.bar(pos, med.values, width=width * 0.88, color=cols, label=WL_SHORT[wl],
            yerr=[(med - lo).values, (hi - med).values], capsize=3)
    span[wl] = (med.min(), med.max())
    # offset in punti: l'etichetta resta staccata dalla barra qualunque sia il range
    for xi, m, h, l in zip(pos, med.values, hi.values, lo.values):
        up = m >= 0
        gap.annotate(f"{m:+.1f}", xy=(xi, h if up else l), xytext=(0, 4 if up else -4),
                     textcoords="offset points", ha="center", fontsize=8,
                     va="bottom" if up else "top")
gap.axhline(0, color="black", lw=0.8)
gap.set_xticks(x); gap.set_xticklabels([str(p) for p in Ps])
gap.set_xlabel("numero di partizioni P")
gap.set_ylabel("(dynamic,1 - static) / static  [%]\n"
               "sotto lo zero dynamic,1 è più veloce", fontsize=10)
gap.set_title("differenza relativa (mediana e min-max su 5 rep)\n"
              "grigio = segno non consistente fra le rep, cioè rumore", fontsize=10)
gap.grid(ls=":", alpha=0.45, axis="y"); gap.set_axisbelow(True)
gap.margins(y=0.14)
gap.legend(fontsize=9, loc="upper right", framealpha=0.92, title="carico", title_fontsize=9)

# il sottotitolo riporta i numeri misurati, non una conclusione decisa a priori
sub_bits = [f"{WL_TITLE[w]}: da {span[w][0]:+.1f}% a {span[w][1]:+.1f}%" for w in WLS]
fig.suptitle("Granularità del join con #pragma omp for: P alto = iterazioni piccole (T=16)\n"
             "dynamic,1 rispetto a static, " + " · ".join(sub_bits),
             fontsize=12.5, y=0.99)
fig.tight_layout(rect=[0, 0, 1, 0.90])
fig.savefig(os.path.join(OUT, "schedule_granularity.png"), dpi=170)
print("ok ->", OUT, "| workload nella fig.2:", WLS)
