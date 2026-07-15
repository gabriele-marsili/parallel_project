#!/usr/bin/env python3
"""Grafici del rerun: CPU throughput vs N e breakdown CUDA con sensibilita' NUMA."""
import os
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "axes.labelsize": 11,
                     "figure.facecolor": "white", "axes.facecolor": "white",
                     "axes.axisbelow": True})

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results"); OUT = os.path.join(HERE, "plots")
os.makedirs(OUT, exist_ok=True)

# --- Fig 1: CPU throughput vs N ---
cpu = pd.read_csv(os.path.join(RES, "cpu_rerun.csv"))
fig, ax = plt.subplots(figsize=(8.5, 5))
for col, c, m in [("baseline", "#d6604d", "o"), ("avx2", "#3690c0", "s"),
                  ("autovec", "#1b7837", "^")]:
    ax.plot(cpu["N"], cpu[col], marker=m, color=c, label=col, lw=1.8)
ax.set_xscale("log")
ax.set_xlabel("N (numero di chiavi, log)")
ax.set_ylabel("throughput (Mkeys/s)")
ax.set_ylim(0, 1500)
ax.set_title("Rerun CPU su node09: throughput vs N (P=256)\n"
             "riproduce la Tabella 1: baseline ~910, autovec ~1330 (1.46×), avx2 ~1200 (1.31×)",
             fontsize=10.5)
ax.legend(); ax.grid(ls=":", alpha=0.4)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "cpu_rerun_vs_N.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

# --- Fig 2: CUDA breakdown + NUMA ---
cu = pd.read_csv(os.path.join(RES, "cuda_numa.csv"))
cu = cu[cu["placement"] != "report (Tab.2)"].reset_index(drop=True)
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.8),
                               gridspec_kw={"width_ratios": [1.3, 1]})
# stacked breakdown
x = np.arange(len(cu))
ax1.bar(x, cu["h2d_ms"], label="H→D (PCIe)", color="#4393c3")
ax1.bar(x, cu["kernel_ms"], bottom=cu["h2d_ms"], label="kernel", color="#1b7837")
ax1.bar(x, cu["d2h_ms"], bottom=cu["h2d_ms"] + cu["kernel_ms"],
        label="D→H (PCIe)", color="#92c5de")
for i in range(len(cu)):
    pcie = cu["h2d_ms"][i] + cu["d2h_ms"][i]
    ax1.text(i, cu["total_ms"][i] + 3,
             f"tot {cu['total_ms'][i]:.0f} ms\nPCIe {100*pcie/cu['total_ms'][i]:.0f}%",
             ha="center", fontsize=8)
ax1.set_xticks(x); ax1.set_xticklabels(cu["placement"], fontsize=9)
ax1.set_ylabel("tempo (ms) a N=10⁸")
ax1.set_title("Breakdown CUDA: il trasferimento PCIe domina (kernel 1.5 ms su circa 100-174 ms totali)",
              fontsize=10)
ax1.legend(fontsize=8); ax1.set_ylim(0, 200)

# e2e vs CPU
labels = list(cu["placement"]) + ["CPU baseline", "CPU autovec"]
vals = list(cu["e2e_Mkeys_s"]) + [911.1, 1330.2]
cols = ["#d6604d", "#1b7837", "#bdbdbd", "#7f7f7f"]
ax2.bar(labels, vals, color=cols, edgecolor="black", linewidth=0.4)
for i, v in enumerate(vals):
    ax2.text(i, v + 15, f"{v:.0f}", ha="center", fontsize=9)
ax2.axhline(911.1, ls="--", color="black", alpha=0.5)
ax2.set_ylabel("throughput end-to-end (Mkeys/s)")
ax2.set_title("GPU end-to-end vs CPU (N=10⁸)\nGPU-far più lenta della CPU, GPU-local circa pari",
              fontsize=10)
ax2.tick_params(axis="x", rotation=20, labelsize=8)
ax2.set_ylim(0, 1500)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "cuda_numa_breakdown.png"), dpi=150, bbox_inches="tight")
print("OK -> cpu_rerun_vs_N.png, cuda_numa_breakdown.png")
