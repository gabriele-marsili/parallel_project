#!/usr/bin/env python3
"""Grafico grid search flag: throughput per configurazione (node09, N=100M, P=256)."""
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "axes.labelsize": 11,
                     "figure.facecolor": "white", "axes.facecolor": "white",
                     "axes.axisbelow": True})

HERE = os.path.dirname(os.path.abspath(__file__))
df = pd.read_csv(os.path.join(HERE, "results", "flags_node09.csv"))
OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

# colore per famiglia: no-vec (rosso), vettorizzato (verde), naive/basso (grigio)
def color(lbl):
    if "naive" in lbl: return "#4d4d4d"
    if "novec" in lbl: return "#d6604d"
    if "O0" in lbl or lbl in ("O1", "O2"): return "#bdbdbd"
    if "autovec" in lbl or "unroll" in lbl or "Ofast" in lbl: return "#1b7837"
    return "#8c96c6"  # O3, O3+march (vettorizzati ma senza march/parziali)

fig, ax = plt.subplots(figsize=(11, 5.4))
x = range(len(df))
bars = ax.bar(x, df["throughput_Mkeys_s"], color=[color(l) for l in df["label"]],
              edgecolor="black", linewidth=0.4)
ax.set_xticks(list(x))
ax.set_xticklabels(df["label"], rotation=30, ha="right", fontsize=9)
for i, v in enumerate(df["throughput_Mkeys_s"]):
    ax.text(i, v + 15, f"{v:.0f}", ha="center", fontsize=8)
ax.set_ylabel("throughput (Mkeys/s)")
ax.set_title("Grid search dei flag di compilazione, kernel scalare plain.cpp (node09, N=10⁸, P=256)\n"
             "grigio = non vettorizzato · rosso = -fno-tree-vectorize · verde = vettorizzato AVX2 consegnato",
             fontsize=10.5)
ax.set_ylim(0, 1500)
ax.grid(axis="y", ls=":", alpha=0.4)

# annotazioni sulle transizioni chiave
def bracket(i, j, text, yoff):
    xi, xj = i, j
    yi = max(df["throughput_Mkeys_s"].iloc[i], df["throughput_Mkeys_s"].iloc[j]) + yoff
    ax.annotate("", xy=(xj, yi), xytext=(xi, yi),
                arrowprops=dict(arrowstyle="<->", color="black"))
    ax.text((xi+xj)/2, yi + 20, text, ha="center", fontsize=8, style="italic")

# O2 (idx2) -> O3 (idx3): la vettorizzazione
bracket(2, 3, "O2→O3: +33% = vettorizzazione", 150)
# baseline novec (idx5) vs autovec (idx6): 1.46x
ax.text(5.5, 480, "baseline vs autovec\n908 → 1325 = 1.46×", ha="center",
        fontsize=8, color="#b2182b",
        bbox=dict(boxstyle="round", fc="white", ec="#b2182b", alpha=0.9))

fig.tight_layout()
fig.savefig(os.path.join(OUT, "flags_gridsearch.png"), dpi=150, bbox_inches="tight")
print("OK ->", os.path.join(OUT, "flags_gridsearch.png"))
