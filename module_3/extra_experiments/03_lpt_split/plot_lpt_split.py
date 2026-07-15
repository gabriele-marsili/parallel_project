#!/usr/bin/env python3
"""Esp.3 — decomposizione del vantaggio task sotto skew.
Figura 1: matrice ordine x split (join, T=16 skew).
Figura 2: tetto strutturale del join senza split, con la curva del limite.
Figura 3: sensibilità alla soglia hot (hotmul)."""
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results"); OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

# f_hot = rho/H + (1-rho)/P: quota di lavoro della partizione hot piu' pesante
F_HOT = 0.9 / 4 + 0.1 / 128

osp = pd.read_csv(os.path.join(RES, "order_split.csv"))
fig, ax = plt.subplots(figsize=(8.6, 4.4))
orders = ["lpt", "natural", "random"]
x = np.arange(len(orders)); w = 0.36
for off, hm, c, lbl in [(-w/2, 8, "#2E7D32", "split intra-partizione attivo"),
                        (w/2, 0, "#9E9E9E", "split disattivato")]:
    med = [osp[(osp.order == o) & (osp.hotmul == hm)]["join_ms"].median() for o in orders]
    std = [osp[(osp.order == o) & (osp.hotmul == hm)]["join_ms"].std() for o in orders]
    ax.bar(x + off, med, width=w, color=c, yerr=std, capsize=3, label=lbl)
    for xi, v in zip(x + off, med):
        ax.text(xi, v + 1.2, f"{v:.0f}", ha="center", fontsize=9)
ax.set_xticks(x); ax.set_xticklabels(["LPT (consegnato)", "naturale 0..P-1", "casuale"])
ax.set_ylabel("tempo della fase join (ms)")
ax.set_title("Ordine di sottomissione e split intra-partizione (task, T=16, skew)", fontsize=11.5)
ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True); ax.legend(fontsize=9.5)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "order_split.png"), dpi=170)
plt.close(fig)

ce = pd.read_csv(os.path.join(RES, "ceiling.csv"))
ce["cfg"] = ce["mode"] + "_hm" + ce["hotmul"].astype(str)
fig, ax = plt.subplots(figsize=(8.6, 4.8))
t1 = ce[ce.threads == 1].groupby("cfg")["join_ms"].median().mean()  # ~uguali a T=1
STYLE = {"task_hm8": ("#2E7D32", "s", "task, split attivo"),
         "task_hm0": ("#9E9E9E", "o", "task, split disattivato"),
         "loop_hm8": ("#1565C0", "^", "loop dynamic,1")}
ts = sorted(ce.threads.unique())
for cfg, (c, mk, lbl) in STYLE.items():
    g = ce[ce.cfg == cfg].groupby("threads")["join_ms"].median().reindex(ts)
    ax.plot(ts, t1 / g.values, marker=mk, color=c, lw=1.8, markersize=6, label=lbl)
ceiling = 1.0 / F_HOT
ax.axhline(ceiling, color="#D32F2F", ls="--", lw=1.6)
ax.text(1.1, ceiling + 0.12, f"limite strutturale 1/f_hot = {ceiling:.1f}",
        color="#D32F2F", fontsize=9.5)
ax.plot(ts, ts, ls=":", color="black", lw=1, label="ideale")
ax.set_xscale("log", base=2); ax.set_xticks(ts); ax.set_xticklabels(ts)
ax.set_ylim(0, 8)
ax.set_xlabel("numero di thread")
ax.set_ylabel("speedup della fase join rispetto a T=1")
ax.set_title("Il tetto della partizione hot e il suo superamento con lo split\n(skew rho=0.9, 4 hot, P=128)", fontsize=11.5)
ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True); ax.legend(fontsize=9.5, loc="upper left")
fig.tight_layout()
fig.savefig(os.path.join(OUT, "ceiling.png"), dpi=170)
plt.close(fig)

hm = pd.read_csv(os.path.join(RES, "hotmul_sweep.csv"))
fig, ax = plt.subplots(figsize=(8.6, 4.4))
g = hm.groupby("hotmul")["join_ms"]
med, std = g.median(), g.std()
# peso della partizione hot in unita' di peso medio: f_hot * P
w_hot = F_HOT * 128
# due regimi separati: lo split e' un if sul peso, quindi il passaggio a w_hot
# e' un gradino, non una salita; la linea non deve congiungere 16 e 32
on = med.index[med.index < w_hot]
off = med.index[med.index > w_hot]
for idx in (on, off):
    ax.errorbar(idx, med[idx].values, yerr=std[idx].values,
                marker="s", color="#2E7D32", lw=1.8, markersize=6, capsize=2.5)
ax.text(2, med[off].mean(), "a sinistra della soglia: split attivo sulle 4 hot\n"
        "a destra: nessuna partizione classificata hot", fontsize=9, color="#2E7D32")
ax.plot([on[-1], w_hot, w_hot, off[0]],
        [med[on[-1]], med[on[-1]], med[off[0]], med[off[0]]],
        color="#2E7D32", ls=":", lw=1.4)
ax.axvline(w_hot, color="#D32F2F", ls="--", lw=1.4)
ax.text(w_hot * 1.05, hm["join_ms"].min() + 1,
        f"peso hot = {w_hot:.0f}x la media: lo split e' un if,\n"
        "il salto avviene qui, a gradino\n(29 non e' un punto misurato)",
        color="#D32F2F", fontsize=9)
ax.set_xscale("log", base=2)
ax.set_xticks(sorted(hm.hotmul.unique())); ax.set_xticklabels(sorted(hm.hotmul.unique()))
ax.set_xlabel("soglia hot (multipli del peso medio di partizione)")
ax.set_ylabel("tempo della fase join (ms)")
ax.set_title("Sensibilita alla soglia hot (task, T=16, skew); il consegnato usa 8x\n"
             "1x-16x equivalenti entro il rumore (mediane 59.4-60.6 ms, range sovrapposti)",
             fontsize=11)
ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "hotmul_sweep.png"), dpi=170)
print("ok ->", OUT)
