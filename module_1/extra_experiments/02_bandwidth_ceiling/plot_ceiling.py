#!/usr/bin/env python3
"""
Grafico: dove si colloca il kernel M1 rispetto ai tetti di banda single-core
MISURATI su node09. Legge results/ceiling_summary.csv, scrive plots/bandwidth_ceiling.png.
"""
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "axes.labelsize": 11,
                     "figure.facecolor": "white", "axes.facecolor": "white",
                     "axes.axisbelow": True})

HERE = os.path.dirname(os.path.abspath(__file__))
df = pd.read_csv(os.path.join(HERE, "results", "ceiling_summary.csv"))
OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

MATCHED = 19.15  # tetto matched al pattern del kernel (copy temporal, 12B)

# ordine dal basso verso l'alto
order = ["baseline", "avx2", "autovec",
         "copy matched (non-temporal)", "copy matched (temporal)",
         "STREAM Scale", "STREAM Triad", "STREAM Copy"]
df = df.set_index("label").reindex(order).reset_index()

colors = []
for _, r in df.iterrows():
    if r["category"] == "kernel":
        colors.append("#2166ac" if r["label"] == "autovec" else "#92c5de")
    elif r["category"] == "matched":
        colors.append("#7f7f7f")
    else:
        colors.append("#bdbdbd")

fig, ax = plt.subplots(figsize=(10, 5.2))
y = range(len(df))
bars = ax.barh(list(y), df["gb_s"], color=colors, edgecolor="black", linewidth=0.4)
ax.set_yticks(list(y))
ax.set_yticklabels(df["label"])
for i, v in enumerate(df["gb_s"]):
    ax.text(v + 0.25, i, f"{v:.1f}", va="center", fontsize=9)

# linea di riferimento = tetto matched
ax.axvline(MATCHED, ls="--", lw=1.3, color="#b2182b", alpha=0.8)
ax.text(MATCHED + 0.15, -0.7, f"tetto matched {MATCHED:.1f} GB/s",
        color="#b2182b", fontsize=9)

# annotazione % autovec vs matched
i_auto = df.index[df["label"] == "autovec"][0]
pct = 100 * df.loc[i_auto, "gb_s"] / MATCHED
ax.annotate(f"autovec = {pct:.0f}% del tetto matched\n(il report dichiara 96%)",
            xy=(df.loc[i_auto, "gb_s"], i_auto), xytext=(3.0, i_auto - 1.4),
            fontsize=9, color="#2166ac",
            arrowprops=dict(arrowstyle="->", color="#2166ac"))

ax.set_xlabel("banda single-core (GB/s), node09 AMD EPYC 7301")
ax.set_title("Kernel M1 rispetto ai tetti di banda single-core misurati su node09\n"
             "grigio = tetti (STREAM + copy matched) · blu = kernel del modulo", fontsize=11)
ax.set_xlim(0, 29)
ax.grid(axis="x", ls=":", alpha=0.4)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "bandwidth_ceiling.png"), dpi=150, bbox_inches="tight")
print("OK ->", os.path.join(OUT, "bandwidth_ceiling.png"))
