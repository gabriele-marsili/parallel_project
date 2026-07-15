#!/usr/bin/env python3
"""Esp.3 — barrier vs thread pool + queue."""
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results"); OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)
CB = "#1565C0"; CP = "#E65100"

# ── Fig 1: pipeline end-to-end barrier vs pool ──
pp = pd.read_csv(os.path.join(RES, "pipeline.csv"))
fig, ax = plt.subplots(figsize=(9, 5.2))
for s, c, mk, nm in [("barrier", CB, "o", "std::barrier (consegnato)"), ("pool", CP, "s", "thread pool + queue")]:
    g = pp[pp["sync"] == s].sort_values("threads")
    ax.plot(g["threads"], g["total_ms"], mk + "-", color=c, label=nm, markersize=8, lw=2.2)
ax.set_xlabel("numero di thread"); ax.set_ylabel("tempo end-to-end pipeline (ms)")
ax.set_title("Pipeline completa end-to-end: barriera e thread pool pareggiano")
ax.legend(fontsize=10); ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
ax.set_xticks(sorted(pp["threads"].unique()))
fig.tight_layout(); fig.savefig(os.path.join(OUT, "pipeline_barrier_vs_pool.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

# ── Fig 2: costo puro di un confine di fase ──
mm = pd.read_csv(os.path.join(RES, "microsync.csv"))
fig, ax = plt.subplots(figsize=(9, 5.2))
for s, c, mk, nm in [("barrier", CB, "o", "std::barrier"), ("pool", CP, "s", "thread pool + queue")]:
    g = mm[mm["sync"] == s].sort_values("threads")
    ax.plot(g["threads"], g["ns_per_phase_sync"], mk + "-", color=c, label=nm, markersize=8, lw=2.2)
    for xx, yy in zip(g["threads"], g["ns_per_phase_sync"]):
        ax.annotate(f"{yy:.0f}", (xx, yy), textcoords="offset points", xytext=(0, 8), ha="center", fontsize=7.5, color=c)
ax.set_xlabel("numero di thread"); ax.set_ylabel("ns per confine di fase (fasi vuote)")
ax.set_title("Costo del solo primitivo di sincronizzazione: coda di task vs barriera")
ax.legend(fontsize=10); ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
ax.set_xticks(sorted(mm["threads"].unique()))
fig.tight_layout(); fig.savefig(os.path.join(OUT, "microsync_overhead.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

print("=== Esp.3 numeri ===")
mp = mm.pivot_table(index="threads", columns="sync", values="ns_per_phase_sync")
for t in mp.index:
    print(f"  t={t}: barrier {mp.loc[t,'barrier']:.0f}ns  pool {mp.loc[t,'pool']:.0f}ns  -> pool {mp.loc[t,'pool']/mp.loc[t,'barrier']:.1f}x piu' caro")
ppv = pp.pivot_table(index="threads", columns="sync", values="total_ms")
print("  pipeline (ms) barrier vs pool:")
for t in ppv.index:
    print(f"    t={t}: barrier {ppv.loc[t,'barrier']:.1f}  pool {ppv.loc[t,'pool']:.1f}")
print("OK -> pipeline_barrier_vs_pool.png, microsync_overhead.png")
