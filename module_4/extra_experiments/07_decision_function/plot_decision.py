#!/usr/bin/env python3
"""Esp.7 - quale algoritmo sceglie la decision function di Open MPI.
Figura 1: rank sweep a 8 nodi, default contro i due algoritmi forzati.
Figura 2: i quattro punti della curva di strong scaling del report (32 rank/nodo),
          con il tetto di banda della rete.
Identita' delle serie: colore + marker + tratteggio (il colore da solo non basta:
verde e rosso hanno DeltaE 6.7 in deuteranopia)."""
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results"); OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

BW = 1.15e9          # banda inter-nodo misurata (esp. 2)
GLOBAL_BYTES = 1.2e9  # 150M record x 8 B

# stile per serie: colore, marker, tratteggio, etichetta
SERIES = {
    "default":      ("#1565C0", "s", "-",  "default (la decision function)"),
    "dyn_linear":   ("#D32F2F", "o", "--", "basic linear forzato"),
    "dyn_pairwise": ("#2E7D32", "^", "-.", "pairwise forzato"),
}

def tetto(nodes, ranks):
    """volume off-node per nodo / banda: il minimo tempo fisicamente possibile"""
    if nodes == 1:
        return None
    rpn = ranks // nodes
    return (GLOBAL_BYTES / nodes) * ((ranks - rpn) / ranks) / BW

# ---------------------------------------------------------------- Figura 1
df = pd.read_csv(os.path.join(RES, "decision.csv"))
ranks = sorted(df.ranks.unique())
fig, ax = plt.subplots(figsize=(9.2, 5.0))
for key, (c, mk, ls, lab) in SERIES.items():
    sub = df[df.label == key]
    med = sub.groupby("ranks")["t_max_s"].median().reindex(ranks)
    ax.plot(ranks, med.values, marker=mk, ls=ls, color=c, lw=2, markersize=7, label=lab, zorder=3)
    # i valori grezzi: mostrano la bimodalita' che la mediana nasconde
    ax.scatter(sub.ranks, sub.t_max_s, color=c, s=9, alpha=0.28, zorder=2, linewidths=0)
ax.axhline(0.114, color="#555555", ls=":", lw=1.5, zorder=1)
ax.text(150, 0.055, "tetto di banda della rete (114 ms)", color="#555555", fontsize=9.5)
# offset in punti: le coordinate dati sono su scala log2 e rendono difficile posizionare
for r, who, dx, dy in [(64, "pairwise", -34, 62), (128, "linear", -18, 74), (256, "pairwise", -62, 30)]:
    med = df[(df.label == "default") & (df.ranks == r)].t_max_s.median()
    ax.annotate(f"il default\nsegue {who}", xy=(r, med), xytext=(dx, dy),
                textcoords="offset points", ha="center", fontsize=8.5, color="#1565C0",
                arrowprops=dict(arrowstyle="->", color="#1565C0", lw=1.1,
                                connectionstyle="arc3,rad=0.15"))
ax.set_xscale("log", base=2); ax.set_xticks(ranks); ax.set_xticklabels(ranks)
ax.set_xlabel("numero di rank (8 nodi, volume globale fisso a 1.2 GB)")
ax.set_ylabel("tempo Alltoallv, max fra i rank (s)")
ax.set_title("La decision function alterna i due algoritmi: a 128 rank sceglie quello sbagliato",
             fontsize=11.5)
ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True); ax.set_ylim(bottom=0)
ax.legend(fontsize=9.5, loc="upper left")
fig.tight_layout()
fig.savefig(os.path.join(OUT, "decision_sweep.png"), dpi=170)
plt.close(fig)

# ---------------------------------------------------------------- Figura 2
# I quattro punti sono configurazioni discrete (cambiano nodi E rank insieme):
# barre raggruppate, non una linea, che suggerirebbe un continuum inesistente.
# I valori grezzi sono in evidenza perche' a 8 nodi la distribuzione e' bimodale
# e la mediana cade nel salto fra i due modi: da sola sarebbe fuorviante.
cv = pd.read_csv(os.path.join(RES, "curve.csv"))
pts = [(1, 32), (2, 64), (4, 128), (8, 256)]
HATCH = {"default": "", "dyn_linear": "//", "dyn_pairwise": "\\\\"}
W = 0.26
fig, ax = plt.subplots(figsize=(10.0, 5.4))
for i, (key, (c, mk, ls, lab)) in enumerate(SERIES.items()):
    xs = [j + (i - 1) * W for j in range(len(pts))]
    med = [cv[(cv.label == key) & (cv.nodes == n)].t_max_s.median() for n, _ in pts]
    ax.bar(xs, med, width=W * 0.92, color=c, alpha=0.82, label=lab, zorder=2,
           hatch=HATCH[key], edgecolor="white", linewidth=1.2)
    for j, (n, _) in enumerate(pts):                       # valori grezzi sopra le barre
        v = cv[(cv.label == key) & (cv.nodes == n)].t_max_s.values
        ax.scatter([xs[j]] * len(v), v, color="#222222", s=7, alpha=0.55, zorder=4, linewidths=0)
for j, (n, r) in enumerate(pts):                            # tetto di banda per gruppo
    t = tetto(n, r)
    if t:
        ax.plot([j - 1.6 * W, j + 1.6 * W], [t, t], color="#222222", ls=":", lw=1.8, zorder=5,
                label="tetto di banda della rete" if j == 1 else None)
ax.annotate("in mediana tutti al tetto:\nma basic linear collassa\nin 4 run su 10", xy=(1 + W, 0.52),
            xytext=(0.16, 0.78), fontsize=8.5, color="#555555",
            arrowprops=dict(arrowstyle="->", color="#555555", lw=1.1))
ax.annotate("il default coincide con basic linear:\n2.7x il tetto, ed è il dip del report.\n"
            "pairwise scenderebbe al 97% del tetto", xy=(2 - W/2, 0.531), xytext=(1.30, 0.99),
            fontsize=9, color="#D32F2F", arrowprops=dict(arrowstyle="->", color="#D32F2F", lw=1.2))
ax.annotate("il default segue pairwise\n(stessa distribuzione),\nbasic linear è peggiore", xy=(3 - W, 0.230),
            xytext=(2.60, 0.75), fontsize=8.5, color="#2E7D32",
            arrowprops=dict(arrowstyle="->", color="#2E7D32", lw=1.2))
ax.set_xticks(range(len(pts)))
ax.set_xticklabels([f"{n} nodo\n{r} rank" if n == 1 else f"{n} nodi\n{r} rank" for n, r in pts])
ax.set_xlabel("configurazione dello strong scaling del report (32 rank per nodo)")
ax.set_ylabel("tempo Alltoallv, max fra i rank (s)")
ax.set_title("Perché lo strong scaling di pure MPI crolla a 4 nodi e risale a 8:\n"
             "la libreria cambia algoritmo, e a 128 rank sceglie quello sbagliato", fontsize=11.5)
ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True); ax.set_ylim(0, 1.25)
ax.legend(fontsize=9.5, loc="upper left")
fig.text(0.5, 0.005, "punti neri: le 10 ripetizioni. A 8 nodi la distribuzione è bimodale "
                     "(circa 0.12 s oppure 0.33-0.55 s), quindi la mediana cade fra i due modi.",
         ha="center", fontsize=8.5, color="#555555")
fig.tight_layout(rect=[0, 0.03, 1, 1])
fig.savefig(os.path.join(OUT, "decision_curve.png"), dpi=170)
print("scritto ->", OUT)
