#!/usr/bin/env python3
"""
Diagramma (slide da orale): perché la SIMD a 64 bit perde.
Stesso registro AVX2 da 256 bit: 8 chiavi a 32 bit con 1 vpmulld, vs 4 chiavi a 64 bit
con la decomposizione a 3 vpmuludq. Schema, non dati.
Output: plots/simd_32_vs_64.png
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, FancyBboxPatch, FancyArrowPatch

plt.rcParams.update({"font.size": 11, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

C_IN = "#9ecae1"    # corsie di input
C_OUT = "#a1d99b"   # corsie di output

def reg(ax, x0, y, n, wtot, h, labels, color, fs=9):
    cw = wtot / n
    for i in range(n):
        ax.add_patch(Rectangle((x0 + i * cw, y), cw, h, facecolor=color,
                     edgecolor="black", lw=1.0))
        ax.text(x0 + i * cw + cw / 2, y + h / 2, labels[i], ha="center",
                va="center", fontsize=fs)
    ax.add_patch(Rectangle((x0, y), wtot, h, fill=False, lw=1.8))
    return x0 + wtot

def opbox(ax, x, y, w, text, color):
    ax.add_patch(FancyBboxPatch((x, y), w, 0.95, boxstyle="round,pad=0.06",
                 facecolor=color, edgecolor="black", lw=1.2))
    ax.text(x + w / 2, y + 0.48, text, ha="center", va="center", fontsize=9.5)
    return x + w

def arrow(ax, x0, x1, y):
    ax.add_patch(FancyArrowPatch((x0, y), (x1, y), arrowstyle="-|>",
                 mutation_scale=16, lw=1.7, color="black"))

fig, ax = plt.subplots(figsize=(13, 6.4))
ax.set_xlim(0, 20); ax.set_ylim(0, 7); ax.axis("off")

# ---- riga 1: 32 bit ----
y1 = 4.6
ax.text(0.2, y1 + 1.35, "Hash 32 bit: vpmulld (istruzione nativa)",
        fontsize=13, weight="bold", color="#1b7837")
ax.text(0.2, y1 + 0.55, "registro AVX2 da 256 bit  =  8 corsie a 32 bit", fontsize=10)
xe = reg(ax, 0.2, y1 - 0.55, 8, 8.2, 0.9,
         [f"k{i}" for i in range(8)], C_IN)
arrow(ax, xe + 0.15, xe + 1.15, y1 - 0.10)
xo = opbox(ax, xe + 1.3, y1 - 0.55, 3.2, "1× vpmulld\n(8 mul in 1 istr.)", "#c7e9c0")
arrow(ax, xo + 0.15, xo + 1.15, y1 - 0.10)
reg(ax, xo + 1.3, y1 - 0.55, 8, 4.0, 0.9, [f"h{i}" for i in range(8)], C_OUT, fs=7.5)
ax.text(19.9, y1 - 0.10, "8 chiavi\nper istruzione", ha="right", va="center",
        fontsize=10, weight="bold", color="#1b7837")

# ---- riga 2: 64 bit ----
y2 = 1.5
ax.text(0.2, y2 + 1.35, "Hash 64 bit: vpmullq non esiste in AVX2",
        fontsize=13, weight="bold", color="#b2182b")
ax.text(0.2, y2 + 0.55, "registro AVX2 da 256 bit  =  solo 4 corsie a 64 bit", fontsize=10)
xe = reg(ax, 0.2, y2 - 0.55, 4, 8.2, 0.9, [f"k{i}" for i in range(4)], C_IN)
arrow(ax, xe + 0.15, xe + 1.15, y2 - 0.10)
xo = opbox(ax, xe + 1.3, y2 - 0.55, 3.2,
           "3× vpmuludq\n+ shift + add\n(mul 64 a mano)", "#fcbba1")
arrow(ax, xo + 0.15, xo + 1.15, y2 - 0.10)
reg(ax, xo + 1.3, y2 - 0.55, 4, 4.0, 0.9, [f"h{i}" for i in range(4)], C_OUT)
ax.text(19.9, y2 - 0.10, "4 chiavi\n~7 istruzioni", ha="right", va="center",
        fontsize=10, weight="bold", color="#b2182b")

ax.text(10, 0.15,
        "Stesso registro da 256 bit: a 64 bit metà chiavi (4 invece di 8) e ~7× lavoro per "
        "mul → la SIMD perde:  avx2_64 = 0.98× (peggiora),  avx2_32 = 1.32× (aiuta).",
        ha="center", va="bottom", fontsize=9.5, style="italic",
        bbox=dict(boxstyle="round", fc="#fffbe6", ec="#bfa100"))

fig.tight_layout()
fig.savefig(os.path.join(OUT, "simd_32_vs_64.png"), dpi=150, bbox_inches="tight")
print("OK ->", os.path.join(OUT, "simd_32_vs_64.png"))
