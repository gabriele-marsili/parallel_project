#!/usr/bin/env python3
"""Esp.8 - il confronto cross-module a configurazioni ottimali.
Figura 1: le varianti di M3 (loop/task, 16/32 thread) ai parametri del confronto.
Figura 2: il confronto del report contro quello a parita' di ottimizzazione.
Identita' delle serie: colore + hatch (il colore da solo non basta)."""
import os, csv, statistics as st
from collections import defaultdict
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results"); OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)
BASE = {"uniform": 4.48, "skewed": 2.30}

def med(path, keyf):
    d = defaultdict(list)
    for r in csv.DictReader(open(os.path.join(RES, path))):
        if r.get("t_total_s"):
            d[keyf(r)].append(float(r["t_total_s"]))
    return {k: st.median(v) for k, v in d.items()}

m3 = med("m3_loop_vs_task_50m.csv", lambda r: (r["impl"], r["workload"], r["threads"]))
m4 = med("m4_hybrid_threads.csv", lambda r: (r["workload"], r["nodes"], r["threads"]))

# ------------------------------------------------------------------ Figura 1
fig, axes = plt.subplots(1, 2, figsize=(11.5, 4.8))
for ax, wl in zip(axes, ["uniform", "skewed"]):
    labels = ["loop\n16 thr", "loop\n32 thr", "task\n16 thr", "task\n32 thr"]
    vals = [m3[("m3_loop", wl, "16")], m3[("m3_loop", wl, "32")],
            m3[("m3_task", wl, "16")], m3[("m3_task", wl, "32")]]
    cols = ["#1565C0", "#1565C0", "#EF6C00", "#EF6C00"]
    hats = ["", "//", "", "//"]
    bars = ax.bar(range(4), vals, color=cols, hatch=hats, alpha=0.85,
                  edgecolor="white", linewidth=1.2)
    best = vals.index(min(vals))
    for i, (b, v) in enumerate(zip(bars, vals)):
        ax.text(b.get_x() + b.get_width()/2, v + 0.012, f"{v:.3f}\n{BASE[wl]/v:.1f}x",
                ha="center", fontsize=9, fontweight="bold" if i == best else "normal")
    ax.set_xticks(range(4)); ax.set_xticklabels(labels, fontsize=9.5)
    ax.set_ylabel("tempo (s)" if wl == "uniform" else "")
    ax.set_title(f"{wl} (baseline {BASE[wl]} s)", fontsize=11)
    ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True)
    ax.set_ylim(0, max(vals) * 1.28)
axes[0].annotate("l'hyper-threading costa il 18%:\n16 thread = 16 core fisici,\n32 = due thread per core",
                 xy=(1, m3[("m3_loop", "uniform", "32")]), xytext=(1.5, 0.14), fontsize=8.5,
                 color="#D32F2F", arrowprops=dict(arrowstyle="->", color="#D32F2F", lw=1.2))
axes[1].annotate("a P=256 la task non serve più:\ncon 256 partizioni il dynamic\nbilancia già da solo",
                 xy=(3, m3[("m3_task", "skewed", "32")]), xytext=(0.15, 0.40), fontsize=8.5,
                 color="#EF6C00", arrowprops=dict(arrowstyle="->", color="#EF6C00", lw=1.2))
fig.suptitle("M3 ai parametri del confronto (NR=50M, P=256): la loop vince ovunque, e 16 thread battono 32",
             fontsize=12)
fig.tight_layout(rect=[0, 0, 1, 0.93])
fig.savefig(os.path.join(OUT, "m3_variants.png"), dpi=170)
plt.close(fig)

# ------------------------------------------------------------------ Figura 2
fig, ax = plt.subplots(figsize=(9.6, 5.2))
groups = [
    ("come nel report", [("M3 1 nodo\n32 thr", m3[("m3_loop", "uniform", "32")], "#1565C0", ""),
                         ("M4 ibrido 8 nodi\n32 thr", m4[("uniform", "8", "32")], "#2E7D32", "")]),
    ("ciascuno al suo meglio", [("M3 1 nodo\n16 thr", m3[("m3_loop", "uniform", "16")], "#1565C0", "//"),
                                ("M4 ibrido 8 nodi\n16 thr", m4[("uniform", "8", "16")], "#2E7D32", "//")]),
]
x = 0; xs = []; xlab = []
for gname, items in groups:
    for lab, v, c, h in items:
        b = ax.bar(x, BASE["uniform"] / v, color=c, hatch=h, alpha=0.85, edgecolor="white",
                   linewidth=1.2, width=0.62)
        ax.text(x, BASE["uniform"]/v + 0.15, f"{BASE['uniform']/v:.1f}x", ha="center",
                fontsize=10.5, fontweight="bold")
        xs.append(x); xlab.append(lab); x += 1
    x += 0.5
ax.set_xticks(xs); ax.set_xticklabels(xlab, fontsize=9.5)
ax.set_ylabel("speedup sulla baseline sequenziale (4.48 s), uniforme")
ax.set_ylim(0, 16.2)
# le barre arrivano a 12.2: le annotazioni stanno sopra, senza coprirle
ax.plot([0, 1], [13.2, 13.2], color="#555555", lw=1.3)
ax.text(0.5, 13.5, "come nel report:\notto volte l'hardware per +20%", ha="center", fontsize=9.5,
        color="#555555")
ax.plot([2.5, 3.5], [13.2, 13.2], color="#D32F2F", lw=1.3)
ax.text(3.0, 13.5, "ciascuno al suo meglio:\notto volte l'hardware per +2.5%", ha="center",
        fontsize=9.5, color="#D32F2F", fontweight="bold")
fig.text(0.5, 0.015, "Il report cita M3 a 32 thread (la sua configurazione peggiore su uniforme) "
                     "e M4 al suo meglio. A 16 thread M3 recupera il 18%.",
         ha="center", fontsize=8.5, color="#555555")
ax.set_title("Il confronto cross-module a parità di ottimizzazione:\nla conclusione del report esce rafforzata",
             fontsize=11.5)
ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True)
fig.tight_layout(rect=[0, 0.04, 1, 1])
fig.savefig(os.path.join(OUT, "crossmodule_fair.png"), dpi=170)
print("scritto ->", OUT)
