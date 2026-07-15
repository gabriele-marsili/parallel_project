#!/usr/bin/env python3
"""Esp.6 — la frazione seriale di Amdahl: come si stima (fit) e perché è APPARENTE.
Fig 1: fit Amdahl su S(p) misurato + residui + R^2 (mostra che il fit è reale).
Fig 2: f fittata vs frazione seriale LETTERALE misurata (merge+prefix), che vanno in
       direzioni OPPOSTE con P -> la f del fit non è codice seriale, è banda.
"""
import os
import numpy as np
import pandas as pd
from scipy.optimize import curve_fit
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results"); OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)
REPORT = os.path.join(HERE, "..", "..", "results")  # CSV consegnati

def amdahl(p, f): return 1.0 / (f + (1.0 - f) / p)

# ── dati strong scaling consegnati (P=128) ──
ss = pd.read_csv(os.path.join(REPORT, "strong_scaling.csv"))
g20 = ss[ss["nr"] == ss["nr"].max()]
p = g20["threads"].values.astype(float)
S = g20["time_seq"].values[0] / g20["time_par"].values
popt, _ = curve_fit(amdahl, p, S, p0=[0.05], bounds=(0, 1))
f = popt[0]
Sfit = amdahl(p, f)
ss_res = np.sum((S - Sfit) ** 2); ss_tot = np.sum((S - S.mean()) ** 2)
R2 = 1 - ss_res / ss_tot

# ── Fig 1: fit + residui ──
fig, (ax, axr) = plt.subplots(2, 1, figsize=(9, 7), height_ratios=[3, 1], sharex=True)
pf = np.linspace(1, 33, 200)
ax.plot(pf, pf, "--", color="#BDBDBD", label="ideale S=p")
ax.plot(pf, amdahl(pf, f), "-", color="#D32F2F", lw=2.3,
        label=f"fit Amdahl  S(p)=1/(f+(1-f)/p)\n f={f:.4f}  ->  S_inf=1/f={1/f:.1f}   (R^2={R2:.4f})")
ax.plot(p, S, "o", color="#1565C0", markersize=10, zorder=5, label="misurato (NR=20M, P=128)")
# due punti per il check analitico
ax.annotate(f"S(2)={S[np.where(p==2)][0]:.2f}", (2, S[np.where(p==2)][0]),
            textcoords="offset points", xytext=(8, -14), fontsize=8, color="#1565C0")
ax.annotate(f"S(32)={S[np.where(p==32)][0]:.2f}", (32, S[np.where(p==32)][0]),
            textcoords="offset points", xytext=(-70, 6), fontsize=8, color="#1565C0")
ax.set_ylabel("speedup S(p)")
ax.set_title("Come si stima f: least-squares del modello Amdahl a 1 parametro sui dati misurati")
ax.legend(fontsize=9.2, loc="upper left"); ax.grid(ls=":", alpha=0.4); ax.set_ylim(0, 34)
# residui
axr.axhline(0, color="#888", lw=1)
axr.stem(p, S - Sfit, linefmt="#7B1FA2", markerfmt="o", basefmt=" ")
axr.set_ylabel("residuo\n(mis - fit)"); axr.set_xlabel("thread p")
axr.grid(ls=":", alpha=0.4)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "amdahl_fit.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

# ── Fig 2: f fittata vs frazione seriale letterale misurata ──
def fit_f(csv):
    d = pd.read_csv(csv); pp = d["threads"].values.astype(float)
    return d
# fit su P=512 consegnato
ss512 = pd.read_csv(os.path.join(REPORT, "strong_scaling_p512.csv"))
g512 = ss512[ss512["nr"] == ss512["nr"].max()]
p5 = g512["threads"].values.astype(float); S5 = g512["time_seq"].values[0] / g512["time_par"].values
f512 = curve_fit(amdahl, p5, S5, p0=[0.05], bounds=(0, 1))[0][0]

sf128 = pd.read_csv(os.path.join(RES, "serial_fraction_p128.csv"))
sf512 = pd.read_csv(os.path.join(RES, "serial_fraction_p512.csv"))

fig, ax = plt.subplots(figsize=(9.5, 5.6))
ax.axhline(f * 100, ls="--", color="#D32F2F", lw=2, label=f"f fittata, P=128 = {f*100:.1f}%  (S_inf={1/f:.0f})")
ax.axhline(f512 * 100, ls="--", color="#E65100", lw=2, label=f"f fittata, P=512 = {f512*100:.1f}%  (S_inf={1/f512:.0f})")
ax.plot(sf128["threads"], sf128["serial_frac"] * 100, "o-", color="#1565C0", lw=2,
        label="seriale letterale misurato, P=128 (merge+prefix)")
ax.plot(sf512["threads"], sf512["serial_frac"] * 100, "s-", color="#2E7D32", lw=2,
        label="seriale letterale misurato, P=512")
ax.set_yscale("log")
ax.set_xlabel("thread p"); ax.set_ylabel("frazione seriale (%, scala log)")
ax.set_title("La f del fit (circa 8%) non è il codice seriale (circa 0.1%): 80x di distanza, direzioni opposte al variare di P")
ax.set_xticks(sf128["threads"])
ax.legend(fontsize=8.5, loc="lower right"); ax.grid(ls=":", alpha=0.45, which="both")
# annotazioni rimosse (ridondanti con legenda e titolo, sovrapponevano le linee)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "serial_fraction.png"), dpi=150, bbox_inches="tight")
plt.close(fig)

print("=== Esp.6 numeri ===")
print(f"  fit P=128: f={f:.4f} S_inf={1/f:.1f} R2={R2:.4f}")
print(f"  fit P=512: f={f512:.4f} S_inf={1/f512:.1f}")
print(f"  seriale letterale P=128 @t=32: {sf128['serial_frac'].iloc[-1]*100:.3f}%")
print(f"  seriale letterale P=512 @t=32: {sf512['serial_frac'].iloc[-1]*100:.3f}%")
# check analitico due punti: f da S(p1),S(p2)
def f_two(p1,S1,p2,S2):
    # 1/S = f + (1-f)/p  ->  risolvi per f
    # da due eq: 1/S1 - 1/S2 = (1-f)(1/p1 - 1/p2)
    a = (1/S1 - 1/S2)/(1/p1 - 1/p2)   # = 1-f
    return 1 - a
S_at = dict(zip(p.astype(int), S))
print(f"  check 2-punti f da S(2),S(32): {f_two(2,S_at[2],32,S_at[32]):.4f}")
print(f"  check 2-punti f da S(4),S(16): {f_two(4,S_at[4],16,S_at[16]):.4f}")
print("OK -> amdahl_fit.png, serial_fraction.png")
