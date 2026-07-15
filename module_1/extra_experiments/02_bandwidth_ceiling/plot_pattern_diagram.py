#!/usr/bin/env python3
"""
Diagramma esplicativo (slide da orale): confronta il pattern di accesso alla memoria
del benchmark STREAM Scale (il "16.4") con quello del kernel del modulo (il matched 19.15).
Evidenzia i byte letti e scritti per elemento. Non usa dati misurati: è uno schema.
Output: plots/access_pattern.png
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

C_READ = "#4393c3"   # blu = letti
C_WRITE = "#e08214"  # arancio = scritti
C_EMPTY = "#e0e0e0"  # grigio = non scritti (byte "risparmiati")

def byte_row(ax, x0, y, n, color, w=0.52, gap=0.06, empty=0, label=None):
    """disegna n celle-byte piene + `empty` celle vuote tratteggiate; ritorna x finale."""
    x = x0
    for i in range(n):
        ax.add_patch(Rectangle((x, y), w, w, facecolor=color, edgecolor="black", lw=0.8))
        x += w + gap
    for i in range(empty):
        ax.add_patch(Rectangle((x, y), w, w, facecolor=C_EMPTY, edgecolor="black",
                               lw=0.8, ls=(0, (2, 2)), alpha=0.6))
        x += w + gap
    if label:
        ax.text(x0 + ((n + empty) * (w + gap) - gap) / 2, y - 0.30, label,
                ha="center", va="top", fontsize=9.5)
    return x

def op_box(ax, x, y, text, color="#f7f7f7"):
    ax.add_patch(FancyBboxPatch((x, y - 0.05), 3.3, 0.95,
                 boxstyle="round,pad=0.08", facecolor=color, edgecolor="black", lw=1.0))
    ax.text(x + 1.65, y + 0.42, text, ha="center", va="center", fontsize=9.5)
    return x + 3.3

def arrow(ax, x0, x1, y):
    ax.add_patch(FancyArrowPatch((x0, y + 0.26), (x1, y + 0.26),
                 arrowstyle="-|>", mutation_scale=15, lw=1.6, color="black"))

fig, ax = plt.subplots(figsize=(13, 6.2))
ax.set_xlim(0, 30); ax.set_ylim(0, 10); ax.axis("off")

# ---------------- riga 1: STREAM Scale (il 16.4) ----------------
y1 = 6.6
ax.text(0.2, y1 + 1.9, "STREAM Scale: il tetto generico, 16.4 GB/s",
        fontsize=13, weight="bold", color="#8c510a")
ax.text(0.2, y1 + 1.25, r"loop:   $a[i] \;=\; q \cdot b[i]$    (array di double, 8 byte)",
        fontsize=11, family="monospace")
# read 8B
x = byte_row(ax, 0.5, y1, 8, C_READ, label="legge 8 B\n(un double da b)")
arrow(ax, x + 0.15, x + 1.3, y1)
# op
xo = op_box(ax, x + 1.5, y1, "× q\n(1 moltipl.)")
arrow(ax, xo + 0.2, xo + 1.35, y1)
# write 8B
xw = byte_row(ax, xo + 1.55, y1, 8, C_WRITE, label="scrive 8 B\n(un double in a)")
# totale
ax.text(xw + 0.6, y1 + 0.55, "= 16 B/elem\n→ 16.34 GB/s", fontsize=11, weight="bold",
        va="center", color="#8c510a")

# ---------------- riga 2: kernel partition map (il matched 19.15) ----------------
y2 = 2.0
ax.text(0.2, y2 + 1.9, "Kernel partition map: il tetto matched, 19.15 GB/s",
        fontsize=13, weight="bold", color="#1b7837")
ax.text(0.2, y2 + 1.25, r"loop:   part_ids[i] = hash(keys[i])   (uint64 in, uint32 out)",
        fontsize=11, family="monospace")
# read 8B (uint64)
x = byte_row(ax, 0.5, y2, 8, C_READ, label="legge 8 B\n(uint64 key)")
arrow(ax, x + 0.15, x + 1.3, y2)
# op
xo = op_box(ax, x + 1.5, y2, "hash\nxor·mul·shift\n(solo registri)")
arrow(ax, xo + 0.2, xo + 1.35, y2)
# write 4B (+4 empty)
xw = byte_row(ax, xo + 1.55, y2, 4, C_WRITE, empty=4,
              label="scrive 4 B\n(uint32, metà)")
ax.text(xw + 0.6, y2 + 0.55, "= 12 B/elem\n→ 19.15 GB/s", fontsize=11, weight="bold",
        va="center", color="#1b7837")

# legenda + morale
ax.add_patch(Rectangle((0.5, 9.1), 0.5, 0.5, facecolor=C_READ, edgecolor="black"))
ax.text(1.15, 9.35, "byte letti", va="center", fontsize=10)
ax.add_patch(Rectangle((4.0, 9.1), 0.5, 0.5, facecolor=C_WRITE, edgecolor="black"))
ax.text(4.65, 9.35, "byte scritti", va="center", fontsize=10)
ax.add_patch(Rectangle((7.8, 9.1), 0.5, 0.5, facecolor=C_EMPTY, edgecolor="black",
             ls=(0, (2, 2))))
ax.text(8.45, 9.35, "byte non scritti (risparmiati dal kernel)", va="center", fontsize=10)

ax.text(15.0, 0.15,
        "Stessa lettura (8 B), ma il kernel scrive solo 4 B invece di 8 → pattern più leggero "
        "→ tetto giusto 19, non 16. Confrontare col 16.4 gonfia la saturazione (96% invece di 83%).",
        ha="center", va="bottom", fontsize=9.5, style="italic",
        bbox=dict(boxstyle="round", fc="#fffbe6", ec="#bfa100"))

fig.tight_layout()
fig.savefig(os.path.join(OUT, "access_pattern.png"), dpi=150, bbox_inches="tight")
print("OK ->", os.path.join(OUT, "access_pattern.png"))
