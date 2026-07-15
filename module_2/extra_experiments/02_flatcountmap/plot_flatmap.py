#!/usr/bin/env python3
"""Esp.2 — anatomia FlatCountMap: impl, load factor, residenza cache, false sharing."""
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

# cache del nodo Ivy Bridge (E5-2640 v2): L2 256 KB/core, L3 20 MB/socket
L2 = 256 * 1024; L3 = 20 * 1024 * 1024

# ── Fig 1: unordered_map vs FlatCountMap, tempo medio per operazione (build e probe) ──
di = pd.read_csv(os.path.join(RES, "flatmap_impl.csv")).set_index("impl")
g = ["umap", "flat2"]
build = [di.loc[k, "build_ns_per_key"] for k in g]
probe = [di.loc[k, "probe_ns_per_key"] for k in g]
x = np.arange(len(g)); w = 0.34
fig, ax = plt.subplots(figsize=(8.6, 5.4))
ax.bar(x - w/2, build, w, label="build (increment)", color=["#D32F2F", "#2E7D32"],
       alpha=0.55, edgecolor="black", lw=0.5, zorder=3)
ax.bar(x + w/2, probe, w, label="probe (count)", color=["#D32F2F", "#2E7D32"],
       edgecolor="black", lw=0.5, hatch="//", zorder=3)
for xi, v in zip(x - w/2, build):
    ax.text(xi, v, f"{v:.1f}", ha="center", va="bottom", fontsize=10)
for xi, v in zip(x + w/2, probe):
    ax.text(xi, v, f"{v:.1f}", ha="center", va="bottom", fontsize=10)
bsp = build[0] / build[1]; psp = probe[0] / probe[1]
ax.set_xlim(-0.55, 1.55)
ax.text(1, max(build[1], probe[1]) + 0.06 * max(build),
        f"probe {psp:.1f}x, build {bsp:.1f}x piu' veloce",
        ha="center", va="bottom", fontsize=10, color="#1b7837", weight="bold")
ax.set_xticks(x)
ax.set_xticklabels(["unordered_map", "FlatCountMap"], fontsize=10)
ax.set_ylabel("ns per operazione su una chiave")
ax.set_title("FlatCountMap vs unordered_map: tempo per operazione (distinct=20k, single core, Ivy Bridge)",
             fontsize=11.5, pad=14)
ax.set_ylim(0, max(build) * 1.20)
ax.legend(fontsize=10); ax.grid(axis="y", ls=":", alpha=0.4); ax.set_axisbelow(True)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "flatmap_impl.png"), dpi=150, bbox_inches="tight")
plt.close(fig)
di = di.reset_index()

# ── Fig 2: residenza cache — ns/probe vs dimensione tabella ──
dc = pd.read_csv(os.path.join(RES, "flatmap_cache.csv"))
fig, ax = plt.subplots(figsize=(9.5, 5.4))
for impl, c, mk, nm in [("flat2", "#2E7D32", "s", "FlatCountMap"),
                        ("umap", "#D32F2F", "o", "unordered_map")]:
    g = dc[dc["impl"] == impl].sort_values("table_bytes")
    ax.plot(g["table_bytes"], g["probe_ns_per_key"], mk + "-", color=c, label=nm, markersize=7, lw=2)
ax.axvline(L2, ls="--", color="#888", lw=1.3); ax.text(L2*1.1, ax.get_ylim()[1]*0.9, "L2 256 KB", rotation=90, fontsize=8, va="top", color="#555")
ax.axvline(L3, ls="--", color="#333", lw=1.3); ax.text(L3*1.1, ax.get_ylim()[1]*0.9, "L3 20 MB", rotation=90, fontsize=8, va="top", color="#333")
ax.set_xscale("log", base=2); ax.set_xlabel("dimensione tabella hash (byte, scala log)")
ax.set_ylabel("ns per probe")
ax.set_title("Costo del probe al crescere della tabella: oltre L3 diventa DRAM-bound")
ax.legend(fontsize=10); ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "flatmap_cache.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

# ── Fig 2b: load factor — probe vs alpha (patologia del linear probing) ──
lf = pd.read_csv(os.path.join(RES, "loadfactor.csv")).sort_values("alpha_actual")
fig, ax = plt.subplots(figsize=(9.5, 5.4))
a = lf["alpha_actual"].values; pns = lf["probe_ns_per_key"].values
ax.set_xlim(0, 1.02); ax.set_ylim(0, max(pns) * 1.12)
ymax = ax.get_ylim()[1]
ax.plot(a, pns, "o-", color="#1565C0", lw=2.2, markersize=8)
ax.axvline(0.5, ls=":", color="#2E7D32", lw=1.8)
ax.axvline(1.0, ls=":", color="#D32F2F", lw=1.8)
ax.text(0.49, ymax * 0.55, "x2: dimensionamento scelto\n(riempimento al piu' 50%)", rotation=90,
        fontsize=9, color="#2E7D32", va="center", ha="right")
ax.text(0.99, ymax * 0.55, "x1: riempimento fino al 100%", rotation=90,
        fontsize=9, color="#7f0000", va="center", ha="right")
ax.set_xlabel("load factor: quanto e' piena la tabella (chiavi distinte / slot)")
ax.set_ylabel("ns per probe (per chiave)")
ax.set_title("Costo del probe al crescere del riempimento: piatto fino al 50%, esplode verso il 100%")
ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "flatmap_loadfactor.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

# ── Fig 3: false sharing padded vs packed ──
fs = pd.read_csv(os.path.join(RES, "false_sharing.csv"))
fig, ax = plt.subplots(figsize=(9, 5.2))
for mode, c, mk, nm in [("packed", "#D32F2F", "o", "packed (accumulatori adiacenti -> false sharing)"),
                        ("padded", "#2E7D32", "s", "padded alignas(64) (codice consegnato)")]:
    g = fs[fs["mode"] == mode].sort_values("threads")
    ax.plot(g["threads"], g["time_ms"], mk + "-", color=c, label=nm, markersize=8, lw=2.2)
    for xx, yy in zip(g["threads"], g["time_ms"]):
        ax.annotate(f"{yy:.0f}", (xx, yy), textcoords="offset points", xytext=(0, 8), ha="center", fontsize=7.5, color=c)
ax.set_xlabel("numero di thread"); ax.set_ylabel("tempo (ms), stesso lavoro totale")
ax.set_title("False sharing: accumulatori per-thread packed vs padded a 64 B")
ax.legend(fontsize=9.5); ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
ax.set_xticks(sorted(fs["threads"].unique()))
fig.tight_layout(); fig.savefig(os.path.join(OUT, "false_sharing.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

print("=== Esp.2 numeri ===")
v = di.set_index("impl")
print(f"  umap probe {v.loc['umap','probe_ns_per_key']:.1f} ns  vs flat2 {v.loc['flat2','probe_ns_per_key']:.1f} ns  -> {v.loc['umap','probe_ns_per_key']/v.loc['flat2','probe_ns_per_key']:.1f}x")
print(f"  flat1 {v.loc['flat1','probe_ns_per_key']:.1f}  flat2 {v.loc['flat2','probe_ns_per_key']:.1f}  flat4 {v.loc['flat4','probe_ns_per_key']:.1f} ns")
fsp = fs.pivot_table(index="threads", columns="mode", values="time_ms")
for t in fsp.index:
    print(f"  t={t}: packed {fsp.loc[t,'packed']:.0f}ms padded {fsp.loc[t,'padded']:.0f}ms -> {fsp.loc[t,'packed']/fsp.loc[t,'padded']:.2f}x")
print("OK -> flatmap_impl.png, flatmap_cache.png, false_sharing.png")
