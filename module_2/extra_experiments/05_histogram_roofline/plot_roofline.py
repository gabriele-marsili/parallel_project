#!/usr/bin/env python3
"""Esp.5 — histogram memory-bound: banda misurata e roofline (I=0.125 op/byte)."""
import os
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results"); OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

bw = pd.read_csv(os.path.join(RES, "mem_bw.csv"))
KCOL = {"read": "#1565C0", "copy": "#7B1FA2", "histo": "#EF5350"}
KLAB = {"read": "read (solo letture, tetto)", "copy": "copy (read+write, STREAM-like)",
        "histo": "histogram (++hist[hash(k)])  <- fase 0/2 vera"}

# ── Fig 1: banda GB/s vs thread ──
fig, ax = plt.subplots(figsize=(9.5, 5.4))
for k in ["read", "copy", "histo"]:
    g = bw[bw["kernel"] == k].sort_values("threads")
    ax.plot(g["threads"], g["GB_s"], "o-", color=KCOL[k], label=KLAB[k], markersize=8, lw=2.2)
    for xx, yy in zip(g["threads"], g["GB_s"]):
        ax.annotate(f"{yy:.0f}", (xx, yy), textcoords="offset points", xytext=(0, 8), ha="center", fontsize=7.5, color=KCOL[k])
ax.set_xlabel("numero di thread"); ax.set_ylabel("banda misurata (GB/s)")
ax.set_title("Banda del nodo Ivy Bridge (N=200M, 1.6 GB): la read satura, l'histogram raggiunge lo stesso tetto a 16 core")
ax.legend(fontsize=9.5, loc="upper left"); ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
ax.set_xticks(sorted(bw["threads"].unique()))
fig.tight_layout(); fig.savefig(os.path.join(OUT, "mem_bandwidth.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

# ── Fig 2: roofline (Gop/s vs OI) col punto histogram a I=0.125 ──
read_peak_bytes = bw[bw["kernel"] == "read"]["GB_s"].max() * 1e9  # tetto di banda misurato (byte/s)
histo_hi = bw[bw["kernel"] == "histo"].sort_values("threads").iloc[-1]
histo_gops = histo_hi["Gop_s"]; histo_oi = 0.125
read_hi = bw[bw["kernel"] == "read"].sort_values("threads").iloc[-1]

fig, ax = plt.subplots(figsize=(9.5, 5.6))
OI = np.logspace(-2.2, 1.2, 200)
mem_roof = read_peak_bytes * OI / 1e9  # Gop/s = (byte/s)*(op/byte)

# Il traffico DRAM (8 B/record) e' fisso; il numero di operazioni contate e' una convenzione.
# Cambiare convenzione muove il punto lungo la diagonale della banda, NON verso il tetto di calcolo:
# I e throughput scalano insieme (throughput = banda x I resta costante). -> memory-bound robusto.
OPS_HI = 5             # hash: >>32, xor, *A32, >>shift, + increment
histo_oi_hi = OPS_HI / 8.0     # ~0.625: contando anche le op della hash
histo_gops_hi = histo_gops * OPS_HI

# tetto di calcolo: NON misurato (il picco di op intere del nodo e' molto piu' alto di questi
# throughput). Lo disegno indicativo/tratteggiato con ginocchio ben oltre I=0.6, cosi' entrambe
# le convenzioni (0.125 e 0.6) cadono sul tratto memory-bound. Non e' un claim quantitativo.
knee_I = 2.0
comp_roof = read_peak_bytes * knee_I / 1e9
ax.plot(OI, mem_roof, "k-", lw=2, label="tetto di banda (= banda x I)")
ax.plot(OI, np.minimum(mem_roof, comp_roof), color="0.55", ls="--", lw=1.4,
        label="tetto di calcolo (indicativo, non misurato)")

ax.axvline(histo_oi, ls="--", color="#EF5350", lw=1.4)
ax.plot([histo_oi], [histo_gops], "o", color="#EF5350", markersize=13, zorder=5,
        label=f"histogram, conv. 1 op/record: I=0.125, {histo_gops:.2f} Gop/s")
ax.plot([histo_oi_hi], [histo_gops_hi], "s", color="#EF5350", markersize=11, zorder=5,
        markerfacecolor="none", markeredgewidth=2,
        label=f"histogram, conv. ~5 op/record: I={histo_oi_hi:.2f}, {histo_gops_hi:.2f} Gop/s")
# linea che collega i due punti: mostra che scivolano sulla stessa diagonale della banda
ax.plot([histo_oi, histo_oi_hi], [histo_gops, histo_gops_hi], ":", color="#EF5350", lw=1.4, zorder=4)
ax.annotate("I = op / 8 byte\n(8 B letti/record e' FISSO;\nle op sono una convenzione:\n1 -> 0.125, ~5 -> ~0.6)\nil punto scorre sulla diagonale\ndella banda, non verso il calcolo",
            xy=(histo_oi, histo_gops), xytext=(histo_oi*1.5, histo_gops*0.28),
            fontsize=8.5, color="#7f0000",
            arrowprops=dict(arrowstyle="->", color="#7f0000"))
ax.set_xscale("log"); ax.set_yscale("log")
ax.set_xlabel("intensita' operazionale  I  (op utili / byte, scala log)")
ax.set_ylabel("throughput (Gop/s, scala log)")
ax.set_title("Roofline: l'histogram è memory-bound per ogni convenzione di conteggio (0.125 e ~0.6 stanno sulla diagonale)")
ax.legend(fontsize=9.5, loc="lower right"); ax.grid(ls=":", alpha=0.4, which="both"); ax.set_axisbelow(True)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "roofline_histogram.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

print("=== Esp.5 numeri ===")
for k in ["read", "copy", "histo"]:
    g = bw[bw["kernel"] == k].sort_values("threads")
    s1 = g[g["threads"] == 1]["GB_s"].iloc[0]; smax = g["GB_s"].max()
    print(f"  {k:6s} 1core={s1:.1f} GB/s  peak={smax:.1f} GB/s (t={int(g.loc[g['GB_s'].idxmax(),'threads'])})")
print(f"  histogram OI=0.125, throughput@peak={histo_gops:.2f} Gop/s")
print("OK -> mem_bandwidth.png, roofline_histogram.png")
