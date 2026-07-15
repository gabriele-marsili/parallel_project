#!/usr/bin/env python3
"""
Grafici per l'esperimento di qualita' della hash di partizionamento (M1, extra).
Legge results/summary_*.csv e results/occupancy_*.csv (prodotti da hash_quality.cpp
eseguito su node09) e genera in plots/:
  - hash_imbalance_bars.png : max/expected per (distribuzione x hash), scala log
  - partition_occupancy.png : occupazione per-partizione, casi di collasso vs fib32
Uso: python3 plot_hash_quality.py
"""
import os
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

plt.rcParams.update({
    "font.size": 11, "axes.titlesize": 12, "axes.labelsize": 11,
    "figure.facecolor": "white", "axes.facecolor": "white",
})

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results")
OUT = os.path.join(HERE, "plots")
os.makedirs(OUT, exist_ok=True)

summary = pd.read_csv(os.path.join(RES, "summary_N100M_P256.csv"))
occ = pd.read_csv(os.path.join(RES, "occupancy_N100M_P256.csv"))
N, P = 100_000_000, 256
EXP = N / P  # 390625

DIST_ORDER = ["uniform", "sequential", "strided_pow2", "low16_zero", "high32_only",
              "dup_1000", "dup_struct"]
DIST_LABEL = {
    "uniform": "uniform\n(random)", "sequential": "sequential\nk=i",
    "strided_pow2": "strided\nk=i·4096", "low16_zero": "low16=0\n(entropia alta)",
    "high32_only": "high32 only\n(bassi=0)", "dup_1000": "dup contigui\nks=1000",
    "dup_struct": "dup struct\n1000·65536",
}
HASH_ORDER = ["fib32", "fib64", "mod", "fib32_lowbits", "mult32_nofold"]
HASH_LABEL = {"fib32": "fib32 (report)", "fib64": "fib64 (a 64 bit)",
              "mod": "mod (solo bit bassi)", "fib32_lowbits": "fib32 bit-bassi",
              "mult32_nofold": "mult32 senza-fold"}
# etichetta "cosa isola" per la heatmap
HASH_ROW = {"fib32": "fib32 (report)\nfold + mul + bit alti",
            "fib64": "fib64\nstessa ricetta, 64 bit",
            "mod": "mod (naive)\nsolo bit bassi chiave",
            "fib32_lowbits": "fib32 low-bits\nbit BASSI del prodotto",
            "mult32_nofold": "mult32 no-fold\nsenza XOR-fold"}
DIST_COL = {"uniform": "uniform\n(random)", "sequential": "sequential\n(k=i)",
            "strided_pow2": "strided\n(k=i·4096)", "low16_zero": "low16=0\n(bit bassi 0)",
            "high32_only": "high32-only\n(bassi 0)", "dup_1000": "dup contigui\n(0…999)",
            "dup_struct": "dup struct\n(·65536)"}
COL = {"fib32": "#1b7837", "fib64": "#66bd63", "mod": "#d73027",
       "fib32_lowbits": "#fdae61", "mult32_nofold": "#7b3294"}

# ===========================================================================
# Fig 1 — max/expected per (dist x hash), scala log (1 = bilanciamento ideale)
# ===========================================================================
piv = summary.pivot(index="dist", columns="hash", values="max_exp").reindex(DIST_ORDER)
fig, ax = plt.subplots(figsize=(11.5, 5.6))
x = np.arange(len(DIST_ORDER))
w = 0.15
for j, h in enumerate(HASH_ORDER):
    vals = piv[h].values
    ax.bar(x + (j - 2) * w, vals, w, label=HASH_LABEL[h], color=COL[h],
           edgecolor="black", linewidth=0.3, zorder=3)
    for xi, v in zip(x + (j - 2) * w, vals):
        if v >= 5:                       # etichetta i collassi, verticale per non sovrapporsi
            ax.text(xi, v * 1.25, f"{v:.0f}×", rotation=90, ha="center",
                    va="bottom", fontsize=8, color="#7f0000", weight="bold")
        elif v >= 1.15:                  # sbilanciamenti intermedi
            ax.text(xi, v * 1.03, f"{v:.1f}", ha="center", va="bottom",
                    fontsize=7, color="#5a3d00")

ax.axhline(1.0, ls="--", lw=1.1, color="black", alpha=0.7, zorder=2)
ax.set_yscale("log")
ax.set_ylim(0.9, 1500)                   # headroom: le etichette "256×" non toccano il titolo
ax.set_ylabel("max(count) / atteso   (scala log; 1 = perfettamente bilanciato)")
ax.set_xticks(x)
ax.set_xticklabels([DIST_LABEL[d] for d in DIST_ORDER], fontsize=9.5)
ax.text(len(DIST_ORDER) - 0.55, 1.04, "ideale", fontsize=8, va="bottom", ha="right",
        style="italic", alpha=0.8)
ax.text(0.012, 0.94, "256× = tutte le 10⁸ chiavi in 1 sola partizione (255 vuote)",
        transform=ax.transAxes, fontsize=8.5, color="#7f0000", va="top")
ax.set_title("Sbilanciamento delle partizioni per funzione hash e distribuzione delle chiavi  "
             "(N=10⁸, P=256, node09)", fontsize=12, pad=12)
ax.legend(ncol=5, fontsize=9, loc="upper center", bbox_to_anchor=(0.5, -0.11),
          frameon=False)
ax.grid(axis="y", ls=":", alpha=0.45, zorder=0)
ax.set_axisbelow(True)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "hash_imbalance_bars.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

# ===========================================================================
# Fig 2 — occupazione per-partizione: fib32 (uniforme) vs mod (collasso)
# ===========================================================================
def counts(dist, h):
    s = occ[(occ["dist"] == dist) & (occ["hash"] == h)].sort_values("partition")
    return s["count"].values.astype(float)

cases = [("high32_only", "high32 only: entropia solo nei 32 bit alti"),
         ("strided_pow2", "strided: chiavi allineate k = i·4096")]
fig, axes = plt.subplots(1, 2, figsize=(12.5, 4.9), sharey=True)
xp = np.arange(P)
for ax, (dist, title) in zip(axes, cases):
    cf = counts(dist, "fib32")
    cm = counts(dist, "mod")

    # fib32: linea piatta = uniforme su tutte le 256 partizioni
    ax.plot(xp, cf, color=COL["fib32"], lw=2.0, zorder=4,
            solid_capstyle="round")
    ax.scatter(xp, cf, s=4, color=COL["fib32"], zorder=5)

    # mod: spike dove c'e' carico (solo partizione 0)
    nz = np.where(cm > 0)[0]
    ax.vlines(nz, 1, cm[nz], color=COL["mod"], lw=4.0, zorder=6)
    ax.scatter(nz, cm[nz], s=45, color=COL["mod"], zorder=7)

    # mod: le 255 partizioni vuote poggiate sul pavimento (count = 0 -> segno a y=1)
    empt = np.where(cm == 0)[0]
    ax.scatter(empt, np.full(empt.size, 1.0), s=8, marker="_",
               color=COL["mod"], alpha=0.55, zorder=3)

    ax.axhline(EXP, ls="--", lw=1.1, color="black", alpha=0.6, zorder=2)
    ax.set_yscale("log")
    ax.set_ylim(0.6, 3e8)
    ax.set_xlim(-6, P + 5)
    ax.set_xlabel("id partizione (0…255)")
    ax.set_title(title, fontsize=11)
    ax.grid(axis="y", ls=":", alpha=0.4, zorder=0)
    ax.set_axisbelow(True)

    # annotazioni
    ax.annotate("mod: 10⁸ chiavi\nin 1 partizione", xy=(0, cm[0]),
                xytext=(30, 2.6e7), fontsize=9, color=COL["mod"], weight="bold",
                arrowprops=dict(arrowstyle="->", color=COL["mod"]))
    ax.annotate("255 partizioni vuote (count = 0)", xy=(150, 1.0),
                xytext=(150, 14), fontsize=8.5, color=COL["mod"], ha="center",
                arrowprops=dict(arrowstyle="->", color=COL["mod"], alpha=0.7))

axes[0].set_ylabel("# chiavi per partizione  (scala log)")
axes[0].annotate("fib32: atteso N/P ≈ 3.9·10⁵ (uniforme, ±1%)",
                 xy=(185, EXP), xytext=(58, 1.4e6), fontsize=9, color=COL["fib32"],
                 weight="bold",
                 arrowprops=dict(arrowstyle="->", color=COL["fib32"]))

# legenda unica
handles = [Line2D([0], [0], color=COL["fib32"], lw=2.2, marker="o", ms=4,
                  label="fib32 (report): uniforme sulle 256 partizioni"),
           Line2D([0], [0], color=COL["mod"], lw=4, marker="o", ms=7,
                  label="mod (naive): tutto in partizione 0")]
fig.legend(handles=handles, loc="upper center", ncol=2, fontsize=10,
           frameon=False, bbox_to_anchor=(0.5, 1.005))
fig.suptitle("Occupazione delle 256 partizioni su chiavi strutturate: fib32 bilancia, mod collassa",
             fontsize=12.5, y=1.08)
fig.tight_layout(rect=[0, 0, 1, 0.97])
fig.savefig(os.path.join(OUT, "partition_occupancy.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

# ===========================================================================
# Fig 3 — matrice hash x distribuzione (heatmap), la piu' leggibile a colpo d'occhio
# ===========================================================================
from matplotlib.colors import LogNorm
M = summary.pivot(index="hash", columns="dist", values="max_exp").reindex(
    index=HASH_ORDER, columns=DIST_ORDER)
fig, ax = plt.subplots(figsize=(10.5, 5.6))
im = ax.imshow(M.values, cmap="RdYlGn_r", norm=LogNorm(vmin=1, vmax=256), aspect="auto")
ax.set_xticks(range(len(DIST_ORDER)))
ax.set_xticklabels([DIST_COL[d] for d in DIST_ORDER], fontsize=9.5)
ax.set_yticks(range(len(HASH_ORDER)))
ax.set_yticklabels([HASH_ROW[h] for h in HASH_ORDER], fontsize=9.5)
for i, h in enumerate(HASH_ORDER):
    for j, d in enumerate(DIST_ORDER):
        v = M.loc[h, d]
        txt = f"{v:.0f}×" if v >= 10 else f"{v:.2f}"
        ax.text(j, i, txt, ha="center", va="center", fontsize=9.5,
                color="white" if v >= 10 else "black",
                weight="bold" if v >= 1.15 else "normal")
# griglia bianca tra le celle
ax.set_xticks(np.arange(-.5, len(DIST_ORDER)), minor=True)
ax.set_yticks(np.arange(-.5, len(HASH_ORDER)), minor=True)
ax.grid(which="minor", color="white", lw=2.5)
ax.tick_params(which="minor", length=0)
cbar = fig.colorbar(im, ax=ax, fraction=0.035, pad=0.02)
cbar.set_label("max / atteso  (scala log)")
cbar.set_ticks([1, 2, 10, 100, 256])
cbar.set_ticklabels(["1\nideale", "2", "10", "100", "256\ncollasso"])
ax.set_title("Sbilanciamento per funzione hash e distribuzione delle chiavi (verde bilanciato, rosso collasso)\n"
             "(riga = funzione hash, colonna = tipo di chiavi; N=10⁸, P=256, node09)",
             fontsize=12, pad=10)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "hash_matrix.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

print("OK ->", os.path.join(OUT, "hash_imbalance_bars.png"))
print("OK ->", os.path.join(OUT, "partition_occupancy.png"))
print("OK ->", os.path.join(OUT, "hash_matrix.png"))
