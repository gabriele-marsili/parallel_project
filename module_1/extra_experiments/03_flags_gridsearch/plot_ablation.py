#!/usr/bin/env python3
"""Ablation dei flag partendo dalla config autovec consegnata (node09, N=100M, P=256).
Mostra il contributo di ogni flag togliendolo/cambiandolo uno alla volta."""
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "axes.labelsize": 11,
                     "figure.facecolor": "white", "axes.facecolor": "white",
                     "axes.axisbelow": True})

HERE = os.path.dirname(os.path.abspath(__file__))
df = pd.read_csv(os.path.join(HERE, "results", "ablation_node09.csv"))
OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

ref = df.loc[df["config"] == "autovec (consegnato)", "throughput_Mkeys_s"].iloc[0]

# ordine dall'alto: autovec, i due che contano, i tre neutri
order = ["autovec (consegnato)", "-- tolgo vettorizzazione", "-- tolgo march=native",
         "-- O3 -> O2", "-- O3 -> O1", "++ aggiungo funroll-loops"]
label = {"autovec (consegnato)": "autovec consegnato\n(riferimento)",
         "-- tolgo vettorizzazione": "− ftree-vectorize\n(tolgo vettorizzazione)",
         "-- tolgo march=native": "− march=native",
         "-- O3 -> O2": "O3 → O2",
         "-- O3 -> O1": "O3 → O1",
         "++ aggiungo funroll-loops": "+ funroll-loops"}
df = df.set_index("config").reindex(order).reset_index()

def color(cfg, tput):
    if cfg == "autovec (consegnato)": return "#1b7837"
    d = (tput - ref) / ref
    if d <= -0.10: return "#b2182b"     # flag che conta molto (grande calo)
    if d <= -0.03: return "#e08214"     # conta
    return "#bdbdbd"                     # neutro

fig, ax = plt.subplots(figsize=(10.5, 5.2))
y = range(len(df))
cols = [color(c, t) for c, t in zip(df["config"], df["throughput_Mkeys_s"])]
ax.barh(list(y), df["throughput_Mkeys_s"], color=cols, edgecolor="black", linewidth=0.4)
ax.invert_yaxis()
ax.set_yticks(list(y)); ax.set_yticklabels([label[c] for c in df["config"]], fontsize=10)

for i, (c, t) in enumerate(zip(df["config"], df["throughput_Mkeys_s"])):
    d = (t - ref) / ref * 100
    txt = f"{t:.0f}" if c == "autovec (consegnato)" else f"{t:.0f}   ({d:+.0f}%)"
    ax.text(t + 12, i, txt, va="center", fontsize=9.5,
            weight="bold" if abs(d) >= 3 or c == "autovec (consegnato)" else "normal")

ax.axvline(ref, ls="--", lw=1.2, color="#1b7837", alpha=0.8)
ax.set_xlabel("throughput (Mkeys/s)")
ax.set_xlim(0, 1600)
ax.set_title("Ablation dei flag di compilazione (node09, N=10⁸, P=256)\n"
             "contano solo −ftree-vectorize (−31%) e −march=native (−15%); il resto è neutro",
             fontsize=11)
ax.grid(axis="x", ls=":", alpha=0.4)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "flags_ablation.png"), dpi=150, bbox_inches="tight")
print("OK ->", os.path.join(OUT, "flags_ablation.png"))
