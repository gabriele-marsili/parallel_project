#!/usr/bin/env python3
"""Esp.2 — diagramma della struttura: unordered_map (chaining) vs FlatCountMap (open addressing).
Schema pedagogico, non dati. Output: plots/flatmap_structure.png
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, FancyArrowPatch, FancyBboxPatch

plt.rcParams.update({"font.size": 10.5, "figure.facecolor": "white", "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

fig, (axL, axR) = plt.subplots(1, 2, figsize=(13.5, 6.6))
for ax in (axL, axR):
    ax.set_xlim(0, 10); ax.set_ylim(0, 10); ax.axis("off")

def box(ax, x, y, w, h, text, fc, fs=9.5, ec="black"):
    ax.add_patch(Rectangle((x, y), w, h, facecolor=fc, edgecolor=ec, lw=1.1))
    ax.text(x + w/2, y + h/2, text, ha="center", va="center", fontsize=fs)

def arrow(ax, x0, y0, x1, y1, color="black", style="-|>"):
    ax.add_patch(FancyArrowPatch((x0, y0), (x1, y1), arrowstyle=style, mutation_scale=13,
                 lw=1.4, color=color))

# ───────────────────── SINISTRA: unordered_map ─────────────────────
axL.text(5, 9.6, "std::unordered_map: separate chaining", ha="center",
         fontsize=11.5, weight="bold", color="#b2182b")
# (etichetta "array di bucket" rimossa: i box gia' dicono "bucket N")
for i in range(6):
    y = 8.2 - i*1.1
    box(axL, 0.6, y, 1.5, 0.85, f"bucket {i}", "#fddbc7", fs=8)
# nodi sparsi nell'heap collegati da puntatori
nodes = {0:[(4.2,7.9),(6.6,6.6)], 1:[(4.6,6.9)], 3:[(4.0,4.9),(6.9,4.2),(5.3,3.0)], 5:[(4.4,2.1)]}
axL.text(6.4, 8.9, "nodi sparsi nell'heap\n(uno per chiave, malloc)", fontsize=8.3, style="italic", ha="center")
for b,(pts) in nodes.items():
    y0 = 8.2 - b*1.1 + 0.42
    prev = (2.1, y0)
    for (nx,ny) in pts:
        box(axL, nx, ny, 1.4, 0.7, "key,cnt\n+ next*", "#f4a582", fs=7)
        arrow(axL, prev[0], prev[1], nx, ny+0.35, color="#b2182b")
        prev = (nx+1.4, ny+0.35)
axL.text(5, 0.7, "lookup = hash -> bucket -> segui i puntatori (pointer chasing) su nodi\n"
                 "sparsi -> cache miss a ogni salto; una malloc per chiave distinta",
         ha="center", fontsize=9, color="#7f0000",
         bbox=dict(boxstyle="round", fc="#fff0ee", ec="#b2182b"))

# ───────────────────── DESTRA: FlatCountMap ─────────────────────
axR.text(5, 9.6, "FlatCountMap: open addressing + linear probing", ha="center",
         fontsize=11.5, weight="bold", color="#1b7837")
axR.text(5, 8.9, "un solo vector contiguo di Slot da 16 byte (4 per cache line da 64 B)",
         fontsize=8.8, style="italic", ha="center")
# una fila di slot contigui
labels = ["k=17\ncnt 2","--\nvuoto","k=42\ncnt 1","k=9\ncnt 3","--\nvuoto","k=88\ncnt 1","--\nvuoto","--\nvuoto"]
fills  = ["#a1d99b","#e8f5e9","#a1d99b","#a1d99b","#e8f5e9","#a1d99b","#e8f5e9","#e8f5e9"]
x0 = 0.7; sw = 1.08
for i,(lab,fc) in enumerate(zip(labels,fills)):
    box(axR, x0+i*sw, 6.2, sw, 1.3, lab, fc, fs=7.5)
# cache line brackets (4 slot)
for grp in range(2):
    gx = x0+grp*4*sw
    axR.add_patch(Rectangle((gx, 6.1), 4*sw, 1.5, fill=False, ec="#1b7837", lw=2.2))
    axR.text(gx+2*sw, 7.75, "1 cache line = 4 slot", fontsize=7.5, ha="center", color="#1b7837")
# slot_of + probing
axR.text(0.7, 5.2, "slot_of(key) = key & mask   (posizione iniziale, bit bassi)", fontsize=9)
arrow(axR, 2.8, 5.0, 2.8, 4.2, color="#1b7837")
axR.text(0.7, 3.7, "se occupato da altra chiave: passa allo slot successivo\n"
                   "h = (h+1) & mask   (linear probing: indirizzo calcolato, non letto)", fontsize=8.8)
arrow(axR, 2.3, 6.15, 3.4, 6.15, color="#1b7837", style="->")
axR.text(5, 0.7, "lookup = un accesso + qualche slot adiacente (spesso stessa cache line).\n"
                 "Zero malloc, zero pointer chasing, il prefetcher predice gli accessi.\n"
                 "Slot 16 B = {key(sentinel ~0=vuoto), cnt}. Tabella = next_pow2(2*|R|) -> load factor <= 50%.",
         ha="center", fontsize=8.8, color="#1b5e20",
         bbox=dict(boxstyle="round", fc="#eef7ee", ec="#1b7837"))

fig.tight_layout()
fig.savefig(os.path.join(OUT, "flatmap_structure.png"), dpi=150, bbox_inches="tight")
print("OK ->", os.path.join(OUT, "flatmap_structure.png"))
