#!/usr/bin/env python3
"""Esp.2 — parametri di Hockney misurati con il ping-pong e verifica del
modello sul weak scaling dell'Alltoallv.
Figura 1: tempo one-way e banda vs taglia del messaggio, inter e intra nodo.
Figura 2: weak scaling dell'Alltoallv misurato contro i due modelli
          (startup (R-1)*alpha del report; volume per NIC beta*V_nodo)."""
import os
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results"); OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

pp = pd.read_csv(os.path.join(RES, "pingpong.csv"))

# fit di Hockney sull'inter-nodo: alpha dai messaggi piccoli, beta dalla banda asintotica
inter = pp[pp.label == "inter"]
ALPHA_US = inter[inter.bytes <= 64]["t_oneway_us"].mean()
BW_MBPS = inter[inter.bytes >= 4 * 1024 * 1024]["bw_MBps"].median()

fig, axes = plt.subplots(1, 2, figsize=(11, 4.5))
for lbl, c in [("inter", "#1565C0"), ("intra", "#EF6C00")]:
    sub = pp[pp.label == lbl].sort_values("bytes")
    axes[0].plot(sub.bytes, sub.t_oneway_us, marker="s", color=c, lw=1.6, markersize=4,
                 label="fra due nodi" if lbl == "inter" else "stesso nodo")
    axes[1].plot(sub.bytes, sub.bw_MBps / 1000, marker="s", color=c, lw=1.6, markersize=4,
                 label="fra due nodi" if lbl == "inter" else "stesso nodo")
axes[0].set_xscale("log", base=2); axes[0].set_yscale("log")
axes[0].set_xlabel("taglia del messaggio (byte)"); axes[0].set_ylabel("tempo one-way (us)")
axes[0].axhline(ALPHA_US, color="#1565C0", ls="--", lw=1.2)
axes[0].text(16, ALPHA_US * 1.35, f"alpha = {ALPHA_US:.0f} us", color="#1565C0", fontsize=9.5)
axes[0].set_title("latenza", fontsize=11)
axes[1].set_xscale("log", base=2)
axes[1].set_xlabel("taglia del messaggio (byte)"); axes[1].set_ylabel("banda (GB/s)")
axes[1].axhline(BW_MBPS / 1000, color="#1565C0", ls="--", lw=1.2)
axes[1].text(64, BW_MBPS / 1000 + 0.15, f"banda di rete = {BW_MBPS/1000:.2f} GB/s",
             color="#1565C0", fontsize=9.5)
axes[1].set_title("banda", fontsize=11)
for ax in axes:
    ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True); ax.legend(fontsize=9.5)
fig.suptitle("Ping-pong: i coefficienti del modello di Hockney misurati sul cluster", fontsize=12.5)
fig.tight_layout(rect=[0, 0, 1, 0.93])
fig.savefig(os.path.join(OUT, "pingpong.png"), dpi=170)
plt.close(fig)

# verifica: weak scaling dell'Alltoallv (dall'esp. 1) contro i due modelli
wk = pd.read_csv(os.path.join(HERE, "..", "01_alltoallv_anatomy", "results", "rank_sweep_weak.csv"))
ranks = sorted(wk.ranks.unique())
med = wk.groupby("ranks")["t_max_s"].median().reindex(ranks)
V_RANK_MB = 6_000_000 * 8 / 1e6  # 48 MB inviati per rank
model_startup = [(r - 1) * ALPHA_US / 1e6 + V_RANK_MB / BW_MBPS for r in ranks]
# volume per NIC: rank per nodo = r/8, quota off-node (r-r/8)/r
model_nic = [(r / 8) * V_RANK_MB * ((r - r / 8) / r) / BW_MBPS for r in ranks]

fig, ax = plt.subplots(figsize=(8.8, 4.8))
ax.plot(ranks, med.values, marker="s", color="#1565C0", lw=2, markersize=7, label="Alltoallv misurato (mediana)")
ax.plot(ranks, model_startup, marker="o", color="#D32F2F", ls="--", lw=1.6, markersize=5,
        label="modello startup: (R-1) alpha + beta V_rank")
ax.plot(ranks, model_nic, marker="^", color="#2E7D32", ls="--", lw=1.6, markersize=5,
        label="modello NIC: beta x volume off-node per nodo")
ax.set_xscale("log", base=2); ax.set_yscale("log")
ax.set_xticks(ranks); ax.set_xticklabels(ranks)
ax.set_xlabel("numero di rank (su 8 nodi, 48 MB inviati per rank)")
ax.set_ylabel("tempo Alltoallv (s)")
ax.set_title("Verifica del modello sul weak scaling: domina il volume per scheda di rete,\nnon il numero di startup", fontsize=11.5)
ax.grid(ls=":", alpha=0.45, which="both"); ax.set_axisbelow(True); ax.legend(fontsize=9.5)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "hockney_check.png"), dpi=170)
print(f"alpha={ALPHA_US:.1f}us  bw={BW_MBPS:.0f}MB/s  ->", OUT)
