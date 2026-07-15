#!/usr/bin/env python3
"""Grafico counterfactual 32 vs 64 bit: throughput scalare vs SIMD (node09)."""
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "axes.labelsize": 11,
                     "figure.facecolor": "white", "axes.facecolor": "white",
                     "axes.axisbelow": True})

HERE = os.path.dirname(os.path.abspath(__file__))
df = pd.read_csv(os.path.join(HERE, "results", "hash64_summary.csv"))
OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

order = ["scalar32", "avx2_32", "autovec32", "scalar64", "avx2_64"]
lbl = {"scalar32":"scalar 32\n(fold+mul)", "avx2_32":"AVX2 32\n(vpmulld)",
       "autovec32":"autovec 32\n(compiler)", "scalar64":"scalar 64\n(1 mul)",
       "avx2_64":"AVX2 64\n(3× vpmuludq)"}
col = {"scalar32":"#bdbdbd","avx2_32":"#1b7837","autovec32":"#5aae61",
       "scalar64":"#bdbdbd","avx2_64":"#d6604d"}
df = df.set_index("kernel").reindex(order).reset_index()

fig, ax = plt.subplots(figsize=(9.5, 5.2))
x = range(len(df))
ax.bar(x, df["throughput_Mkeys_s"], color=[col[k] for k in df["kernel"]],
       edgecolor="black", linewidth=0.4)
ax.set_xticks(list(x)); ax.set_xticklabels([lbl[k] for k in df["kernel"]], fontsize=9)
for i, v in enumerate(df["throughput_Mkeys_s"]):
    ax.text(i, v + 12, f"{v:.0f}", ha="center", fontsize=9)

# separatore 32 vs 64 bit
ax.axvline(2.5, ls=":", color="black", alpha=0.5)
ax.text(1.0, 1420, "hash 32-bit", ha="center", fontsize=10, weight="bold")
ax.text(3.5, 1420, "hash 64-bit", ha="center", fontsize=10, weight="bold")

# speedup SIMD vs scalare, per larghezza
ax.annotate("SIMD 1.32×\n(aiuta)", xy=(1, 1199), xytext=(1, 700),
            ha="center", fontsize=9, color="#1b7837",
            arrowprops=dict(arrowstyle="->", color="#1b7837"))
ax.annotate("SIMD 0.98×\n(peggiora)", xy=(4, 1075), xytext=(4, 620),
            ha="center", fontsize=9, color="#b2182b",
            arrowprops=dict(arrowstyle="->", color="#b2182b"))

ax.set_ylabel("throughput (Mkeys/s)")
ax.set_ylim(0, 1500)
ax.set_title("Hash a 32 vs 64 bit, scalare e SIMD: la vettorizzazione accelera i 32 bit (vpmulld nativo)\n"
             "e rallenta i 64 bit (3× vpmuludq). node09, N=10⁸, P=256", fontsize=11)
ax.grid(axis="y", ls=":", alpha=0.4)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "hash32_vs_hash64.png"), dpi=150, bbox_inches="tight")
print("OK ->", os.path.join(OUT, "hash32_vs_hash64.png"))
