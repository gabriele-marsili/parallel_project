#!/usr/bin/env python3
"""Esp. 9 — figure del confronto M2 vs M3 a parita' di parametri.

fair_time / fair_speedup / fair_efficiency: le tre quantita' della fig. 6 del report, una
       figura per file, a parita' di max_key (5M, il carico del report) e con la stessa
       statistica per i due moduli.
fair_vs_mixed: il rapporto M2/M3-loop. Confronta il gap del report (misto: M2 a
       max_key=1M contro M3 a 5M) con i due gap a parita'. Mostra la sottostima e il
       punto di arrivo a T=32.
"""
import csv
import os
import statistics as st
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "plots")
os.makedirs(OUT, exist_ok=True)

TS = [1, 2, 4, 8, 16, 32]
STYLE = {
    "m2_par":  ("#7f8c8d", "o", "Mod2 (std::thread)"),
    "m3_loop": ("#2874a6", "s", "Mod3 loop"),
    "m3_task": ("#e67e22", "^", "Mod3 task"),
}

d = defaultdict(list)
for r in csv.DictReader(open(os.path.join(HERE, "results", "fair_compare.csv"))):
    if r["t_s"]:
        d[(r["impl"], int(r["max_key"]), int(r["threads"]))].append(float(r["t_s"]))

mean = lambda k: st.mean(d[k]) if d.get(k) else None


def fig_like_report(mk=5000000):
    """Tre figure separate (tempo, speedup, efficienza) a parita' di carico.

    Stesse quantita' della fig. 6 del report, una per file: a pagina intera le curve
    restano distinguibili anche dove si accavallano (loop e task fino a T=2).
    """
    base = mean(("m3_seq", mk, 1))  # baseline condivisa, dichiarata in didascalia
    sub = (f"max_key={mk//10**6}M per entrambi, media di 5 rep. "
           f"Verticale: saturazione dei core fisici.")

    panels = [
        ("fair_time",       "tempo [s]",           "Tempo assoluto",
         lambda ts, v: v,                    "log",   None),
        ("fair_speedup",    "speedup",             f"Speedup su baseline condivisa m3_seq={base:.3f} s",
         lambda ts, v: [base / x for x in v], "log2", [1, 2, 4, 8, 16]),
        ("fair_efficiency", "efficienza",          "Efficienza = speedup / T",
         lambda ts, v: [base / x / t for x, t in zip(v, ts)], None, None),
    ]

    for fname, ylab, title, transform, yscale, yticks in panels:
        fig, ax = plt.subplots(figsize=(8, 5))
        for impl, (color, marker, label) in STYLE.items():
            ts = [t for t in TS if mean((impl, mk, t))]
            v = [mean((impl, mk, t)) for t in ts]
            ax.plot(ts, transform(ts, v), marker=marker, color=color, label=label)
        if fname == "fair_speedup":
            ax.plot(TS, TS, "--", lw=0.8, color="grey", label="ideale")
        if fname == "fair_efficiency":
            ax.axhline(1.0, ls="--", lw=0.8, color="grey")
        ax.axvline(16, ls=":", lw=0.8, color="grey")
        ax.set_xscale("log", base=2)
        ax.set_xticks(TS)
        ax.get_xaxis().set_major_formatter(matplotlib.ticker.ScalarFormatter())
        if yscale == "log":
            ax.set_yscale("log")
        elif yscale == "log2":
            ax.set_yscale("log", base=2)
        if yticks:
            ax.set_yticks(yticks)
            ax.get_yaxis().set_major_formatter(matplotlib.ticker.ScalarFormatter())
        ax.set_xlabel("thread")
        ax.set_ylabel(ylab)
        ax.set_title(f"{title}\n{sub}", fontsize=10)
        ax.grid(alpha=0.3)
        ax.legend(fontsize=9)
        fig.tight_layout()
        fig.savefig(os.path.join(OUT, f"{fname}.png"), dpi=150)
        plt.close(fig)


def fig_vs_mixed():
    """Il gap del report contro i gap a parita'."""
    fig, ax = plt.subplots(figsize=(7, 4.3))
    series = [
        (lambda t: mean(("m2_par", 1000000, t)) / mean(("m3_loop", 5000000, t)),
         "#c0392b", "o", "confronto del report (M2 a 1M vs M3 a 5M)"),
        (lambda t: mean(("m2_par", 1000000, t)) / mean(("m3_loop", 1000000, t)),
         "#2874a6", "s", "a parità, max_key=1M"),
        (lambda t: mean(("m2_par", 5000000, t)) / mean(("m3_loop", 5000000, t)),
         "#27ae60", "^", "a parità, max_key=5M"),
    ]
    for fn, color, marker, label in series:
        ax.plot(TS, [fn(t) for t in TS], marker=marker, color=color, label=label)
    ax.axhline(1.0, ls="--", lw=0.8, color="grey")
    ax.annotate("la convergenza a T=32\nè un artefatto", xy=(32, 1.03), xytext=(9, 1.15),
                fontsize=8, arrowprops=dict(arrowstyle="->", lw=0.8, color="#c0392b"))
    ax.set_xscale("log", base=2)
    ax.set_xticks(TS)
    ax.get_xaxis().set_major_formatter(matplotlib.ticker.ScalarFormatter())
    ax.set_xlabel("thread")
    ax.set_ylabel("tempo M2 / tempo M3-loop")
    ax.set_title("Quanto M3 batte M2: confronto misto contro confronto a parità", fontsize=10)
    ax.grid(alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "fair_vs_mixed.png"), dpi=150)


if __name__ == "__main__":
    fig_like_report()
    fig_vs_mixed()
    print(f"{'T':>4}{'report (misto)':>17}{'parità 1M':>12}{'parità 5M':>12}")
    for t in TS:
        a = mean(("m2_par", 1000000, t)) / mean(("m3_loop", 5000000, t))
        b = mean(("m2_par", 1000000, t)) / mean(("m3_loop", 1000000, t))
        c = mean(("m2_par", 5000000, t)) / mean(("m3_loop", 5000000, t))
        print(f"{t:>4}{a:>16.2f}x{b:>11.2f}x{c:>11.2f}x")
    print(f"\nfigure in {OUT}")
