#!/usr/bin/env python3
"""Esp.4 (extra) — tempo della fase join vs numero di thread, per strategia di assegnamento.
Legge results/join_lb_threadsweep.csv (prodotto da run_join_lb_threadsweep.sh sul cluster).
Pannello sinistro: carico uniforme (skew=0). Pannello destro: carico skewed (0.9, hot=4)."""
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results"); OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

CSV = os.path.join(RES, "join_lb_threadsweep.csv")
if not os.path.exists(CSV):
    raise SystemExit(f"manca {CSV}: esegui prima run_join_lb_threadsweep.sh sul cluster")
df = pd.read_csv(CSV)

STYLE = {"cyclic": ("#2E7D32", "s", "cyclic"),
         "block":  ("#D32F2F", "o", "block"),
         "dynamic": ("#1565C0", "^", "dynamic"),
         "lpt":    ("#7B1FA2", "D", "lpt")}
ORDER = ["cyclic", "block", "dynamic", "lpt"]
ticks = sorted(df["threads"].unique())


def panel(ax, rho, title):
    sub = df[abs(df["skew"] - rho) < 1e-9]
    for s in ORDER:
        g = sub[sub["strategy"] == s].sort_values("threads")
        if g.empty:
            continue
        c, mk, nm = STYLE[s]
        ax.errorbar(g["threads"], g["wall_mean_ms"], yerr=g["wall_std_ms"],
                    marker=mk, color=c, label=nm, lw=2, markersize=7, capsize=3)
    ax.set_xlabel("numero di thread")
    ax.set_ylabel("tempo della fase join (ms)")
    ax.set_title(title, fontsize=11)
    ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
    ax.set_xticks(ticks)
    ax.legend(fontsize=9.5)


fig, (a1, a2) = plt.subplots(1, 2, figsize=(13, 5.2))
panel(a1, 0.0, "Carico uniforme (skew=0)")
panel(a2, 0.9, "Carico skewed (skew=0.9, hot=4)")
fig.suptitle("Tempo della fase join vs numero di thread, per strategia (NR=10M, P=128, node Ivy Bridge)",
             fontsize=12.5, y=1.02)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "join_lb_threads.png"), dpi=150, bbox_inches="tight")
plt.close(fig)
print("OK -> join_lb_threads.png")
