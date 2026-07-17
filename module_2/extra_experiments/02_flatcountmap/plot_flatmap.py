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

# ── Fig 2b: load factor — TRE FIGURE SEPARATE, una unita' di misura ciascuna ──
# NIENTE assi gemelli: sovrapporre ns e probe su due scale indipendenti rende "sopra"/"sotto"
# un artefatto della scala scelta, non un fatto. Ogni figura ha la sua unita' e, nei documenti,
# la sua didascalia.
lf = pd.read_csv(os.path.join(RES, "loadfactor.csv")).sort_values("alpha_actual")
pc = pd.read_csv(os.path.join(RES, "probe_count.csv")).sort_values("alpha")
m = pd.merge(lf.assign(a=lf["alpha_actual"].round(2)), pc.assign(a=pc["alpha"].round(2)), on="a")
assert len(m) == len(lf), f"merge loadfactor/probe_count incompleto: {len(m)} vs {len(lf)}"

a = m["a"].values
pns = m["probe_ns_per_key"].values
cnt = m["probe_medio_contato"].values
knu = m["probe_medio_knuth"].values
ns_per_probe = pns / cnt

# TRE FIGURE SEPARATE, non tre pannelli in una: cosi' ognuna ha la sua didascalia nel documento
# (la formula di Knuth sta sotto al grafico che la riguarda, non in un blocco unico per tre).
XLAB = "load factor: chiavi distinte / slot della tabella"

# --- 1. probe per lookup: conteggio misurato contro il modello ---
fig, ax = plt.subplots(figsize=(9.5, 5.3))
agrid = np.linspace(0.05, 0.985, 300)
ax.plot(agrid, 0.5 * (1 + 1 / (1 - agrid)), "-", color="#9E9E9E", lw=2.6,
        label=r"modello di Knuth: $\frac{1}{2}\left(1 + \frac{1}{1-\alpha}\right)$  (ricerca con successo)")
ax.plot(a, cnt, "o", color="#1565C0", markersize=10,
        label="probe contati")
ax.set_yscale("log"); ax.set_xlim(0, 1.02)
ax.set_xlabel(XLAB)
ax.set_ylabel("probe medi per lookup, cioe' per chiave cercata\n(scala logaritmica)")
ax.set_title("Probe per lookup: conteggio misurato e modello di Knuth", fontsize=12.5, pad=10)
ax.legend(fontsize=10, loc="upper left")
ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "flatmap_lf_probes.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

# --- 2. costo del probe misurato ---
fig, ax = plt.subplots(figsize=(9.5, 5.3))
ax.plot(a, pns, "o-", color="#1565C0", lw=2.4, markersize=9)
ax.set_xlim(0, 1.02); ax.set_ylim(0, max(pns) * 1.16)
ymax = ax.get_ylim()[1]
ax.axvline(0.5, ls=":", color="#2E7D32", lw=2.0)
ax.axvline(1.0, ls=":", color="#D32F2F", lw=2.0)
ax.annotate("tetto garantito dal sizing x2", xy=(0.49, ymax * 0.88),
            xytext=(0.07, ymax * 0.88), fontsize=10, color="#2E7D32", va="center", ha="left",
            arrowprops=dict(arrowstyle="->", color="#2E7D32", lw=1.4, shrinkA=30, shrinkB=2))
ax.annotate("alpha = 1: tabella piena\n(limite del sizing x1)", xy=(0.995, ymax * 0.45),
            xytext=(0.80, ymax * 0.45), fontsize=10, color="#7f0000", va="center", ha="right",
            arrowprops=dict(arrowstyle="->", color="#7f0000", lw=1.4, shrinkB=2))
ax.set_xlabel(XLAB)
ax.set_ylabel("ns per lookup")
ax.set_title("Costo del probe al variare del load factor (node02, tabella fissa a 2 MB)",
             fontsize=12.5, pad=10)
ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "flatmap_lf_time.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

# --- 3. PERCHE' il tempo non e' proporzionale ai probe: decomposizione a due costi ---
# Una lookup = 1 accesso iniziale in una posizione casuale (costo C_first, dipende da quanto
# footprint e' stato toccato, quindi da dove risiede la linea) + (probe-1) accessi di SEGUITO,
# che cadono nello slot successivo: sequenziali e con l'indirizzo CALCOLATO ((h+1)&mask), non
# letto da memoria, quindi senza la catena di dipendenze del pointer chasing (costo C_next).
# NB: NON e' vero che stanno 'nella stessa cache line': ad alpha=0.98 la catena media e' 23.3
# slot, cioe' ~5.8 linee. Il costo basso e' throughput-bound, non 'tutto in una linea'.
#   ns(alpha) = C_first(alpha) + (probe(alpha) - 1) * C_next
# C_next lo stimo dalla coda (0.90 -> 0.98), dove il footprint e' saturo e quindi C_first e'
# costante: li' la pendenza ns/probe isola proprio il costo di un probe di seguito.
C_next = (pns[-1] - pns[-3]) / (cnt[-1] - cnt[-3])   # alpha 0.90 -> 0.98
C_first = pns - (cnt - 1) * C_next

fig, ax = plt.subplots(figsize=(9.5, 5.3))
ax.plot(a, C_first, "o-", color="#C62828", lw=2.4, markersize=9,
        label="costo del 1' accesso della lookup (posizione casuale)")
ax.axhline(C_next, ls="--", color="#1565C0", lw=2.2,
           label=f"costo di ogni accesso SUCCESSIVO (sequenziale, indirizzo calcolato): {C_next:.2f} ns")
ax.set_xlim(0, 1.02); ax.set_ylim(0, max(C_first) * 1.34)
ymax = ax.get_ylim()[1]
ax.annotate(f"solo {m['kb_toccati'].values[0]:.0f} KB della tabella toccati:\n"
            f"la linea e' spesso ancora in cache vicina",
            xy=(a[0], C_first[0] * 1.12), xytext=(0.05, ymax * 0.55),
            fontsize=9.5, color="#616161", ha="left", va="center",
            arrowprops=dict(arrowstyle="->", color="#9E9E9E", lw=1.2, shrinkB=4))
ax.annotate(f"satura a ~{C_first[-1]:.0f} ns quando il footprint satura\n"
            f"({m['kb_toccati'].values[-1]:.0f} KB = tutta la tabella): ordine di una latenza L3",
            xy=(0.925, C_first[-1] * 1.04), xytext=(0.33, ymax * 0.90),
            fontsize=9.5, color="#616161", ha="left", va="center",
            arrowprops=dict(arrowstyle="->", color="#9E9E9E", lw=1.2, shrinkA=8, shrinkB=6))
ax.set_xlabel(XLAB)
# L'unita' qui e' il SINGOLO accesso, non la lookup: una lookup costa
# C_primo + (probe-1)*C_seguito, ed e' il grafico precedente. Dirlo, altrimenti "ns" e' ambiguo.
ax.set_ylabel("ns per singolo accesso a uno slot\n(non per lookup: una lookup ne fa 1 + (probe-1))")
ax.set_title("Perche' il tempo non segue i probe: i due tipi di accesso costano molto diverso",
             fontsize=12.5, pad=10)
ax.legend(fontsize=9.5, loc="lower right", framealpha=0.95)
ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "flatmap_lf_cost.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

print("\n=== Esp.2 decomposizione a due costi ===")
print(f"  C_next (probe di seguito, stimato su alpha 0.90->0.98) = {C_next:.3f} ns")
for i in range(len(a)):
    print(f"  alpha={a[i]:.2f}  C_first={C_first[i]:6.2f} ns   KB toccati={m['kb_toccati'].values[i]:5.0f}")
print(f"  controllo indipendente: alpha=0.95 NON e' usato nel fit -> C_first={C_first[-2]:.2f} ns, "
      f"scarto {abs(C_first[-2]-C_first[-1])/C_first[-1]*100:.1f}% da alpha=0.98")

print("=== Esp.2 load factor: probe contati vs Knuth ===")
for i in range(len(a)):
    print(f"  alpha={a[i]:.2f}  contati={cnt[i]:6.3f}  knuth={knu[i]:6.3f}  "
          f"ns={pns[i]:5.2f}  ns/probe={ns_per_probe[i]:.2f}  KB toccati={m['kb_toccati'].values[i]:.0f}")
_hi, _lo = 9, 7  # alpha=0.98 e alpha=0.90
print(f"  costo marginale dei probe extra (alpha 0.90 -> 0.98): "
      f"{(pns[_hi]-pns[_lo])/(cnt[_hi]-cnt[_lo]):.2f} ns per probe")

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
