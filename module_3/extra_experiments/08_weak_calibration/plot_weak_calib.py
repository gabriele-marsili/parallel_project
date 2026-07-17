#!/usr/bin/env python3
"""Esp. 8 — figure della calibrazione del weak scaling.

fig 1: efficienza weak T(1)/T(p) sui tre bracci (il braccio A e' quello del report)
fig 2: tempo del join per thread sui tre bracci
fig 3: load factor effettivo e ns per chiave del probe (thread singolo)
"""
import csv
import os
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results")
OUT = os.path.join(HERE, "plots")
os.makedirs(OUT, exist_ok=True)

ARMS = ["A", "B", "C"]
LABEL = {
    "A": "A: max_key fisso, P fisso (report)",
    "B": "B: max_key ~ T, P fisso (alpha costante)",
    "C": "C: max_key ~ T, P ~ T (iso-granulare)",
}
COLOR = {"A": "#c0392b", "B": "#e67e22", "C": "#2874a6"}


def mean(xs):
    return sum(xs) / len(xs) if xs else float("nan")


def load_weak():
    """(arm, mode) -> {T: {campo: media sulle rep}}"""
    path = os.path.join(RES, "weak_calib.csv")
    acc = defaultdict(lambda: defaultdict(list))
    with open(path) as fh:
        for row in csv.DictReader(fh):
            key = (row["arm"], row["mode"], int(row["threads"]))
            acc[key]["total"].append(float(row["total_ms"]))
            acc[key]["join"].append(float(row["join_ms"]))
    out = defaultdict(dict)
    for (arm, mode, t), fields in acc.items():
        out[(arm, mode)][t] = {k: mean(v) for k, v in fields.items()}
    return out


def load_alpha():
    """arm -> {T: {campo: media sulle rep}}"""
    path = os.path.join(RES, "alpha_probe.csv")
    acc = defaultdict(lambda: defaultdict(list))
    with open(path) as fh:
        for row in csv.DictReader(fh):
            key = (row["arm"], int(row["threads"]))
            acc[key]["alpha"].append(float(row["alpha"]))
            acc[key]["probe"].append(float(row["probe_ns_per_key"]))
            acc[key]["table_kb"].append(float(row["table_kb"]))
    out = defaultdict(dict)
    for (arm, t), fields in acc.items():
        out[arm][t] = {k: mean(v) for k, v in fields.items()}
    return out


def fig_efficiency(weak):
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.2), sharey=True)
    for ax, mode in zip(axes, ["loop", "task"]):
        for arm in ARMS:
            d = weak.get((arm, mode))
            if not d:
                continue
            ts = sorted(d)
            base = d[ts[0]]["total"]
            ax.plot(ts, [base / d[t]["total"] for t in ts], "o-",
                    color=COLOR[arm], label=LABEL[arm])
        ax.axhline(1.0, ls="--", lw=0.8, color="grey")
        ax.set_xscale("log", base=2)
        ax.set_xticks([1, 2, 4, 8, 16, 32])
        ax.get_xaxis().set_major_formatter(matplotlib.ticker.ScalarFormatter())
        ax.set_xlabel("thread")
        ax.set_title(mode)
        ax.grid(alpha=0.3)
    axes[0].set_ylabel("efficienza weak  T(1)/T(p)")
    axes[0].legend(fontsize=8)
    fig.suptitle("Weak scaling: l'efficienza sopra 1 sopravvive alla ricalibrazione?")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "weak_efficiency_arms.png"), dpi=150)


def fig_join(weak):
    fig, ax = plt.subplots(figsize=(6.5, 4.2))
    for arm in ARMS:
        d = weak.get((arm, "loop"))
        if not d:
            continue
        ts = sorted(d)
        ax.plot(ts, [d[t]["join"] for t in ts], "o-", color=COLOR[arm],
                label=LABEL[arm])
    ax.set_xscale("log", base=2)
    ax.set_xticks([1, 2, 4, 8, 16, 32])
    ax.get_xaxis().set_major_formatter(matplotlib.ticker.ScalarFormatter())
    ax.set_xlabel("thread")
    ax.set_ylabel("join (ms)")
    ax.set_title("Fase join, lavoro nominale per thread costante (loop, uniform)")
    ax.grid(alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "weak_join_arms.png"), dpi=150)


def fig_alpha(alpha):
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.2))
    for arm in ARMS:
        d = alpha.get(arm)
        if not d:
            continue
        ts = sorted(d)
        axes[0].plot(ts, [d[t]["alpha"] for t in ts], "o-", color=COLOR[arm],
                     label=LABEL[arm])
        axes[1].plot(ts, [d[t]["probe"] for t in ts], "o-", color=COLOR[arm],
                     label=LABEL[arm])
    axes[0].set_ylabel("load factor effettivo")
    axes[0].set_title("alpha della FlatCountMap per partizione")
    axes[1].set_ylabel("probe (ns per chiave)")
    axes[1].set_title("costo del probe, thread singolo")
    for ax in axes:
        ax.set_xscale("log", base=2)
        ax.set_xticks([1, 2, 4, 8, 16, 32])
        ax.get_xaxis().set_major_formatter(matplotlib.ticker.ScalarFormatter())
        ax.set_xlabel("thread (via NR = 2M x T)")
        ax.grid(alpha=0.3)
    axes[0].legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "weak_alpha_arms.png"), dpi=150)


if __name__ == "__main__":
    weak = load_weak()
    fig_efficiency(weak)
    fig_join(weak)
    fig_alpha(load_alpha())

    # Tabellina di sintesi: l'efficienza nei punti dove il report vede >100%.
    print("eff. weak (loop, uniform)")
    print(f"{'T':>4}" + "".join(f"{a:>9}" for a in ARMS))
    for t in [1, 2, 4, 8, 16, 32]:
        cells = []
        for arm in ARMS:
            d = weak.get((arm, "loop"), {})
            if t in d and 1 in d:
                cells.append(f"{d[1]['total'] / d[t]['total']:>9.3f}")
            else:
                cells.append(f"{'-':>9}")
        print(f"{t:>4}" + "".join(cells))
    print(f"\nfigure in {OUT}")
