import os
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

BASE_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CSV_PATH = os.path.join(BASE_DIR, 'results', 'cluster', 'schedule_sensitivity.csv')
REPORT_DIR = os.path.join(BASE_DIR, 'report')

try:
    plt.style.use('seaborn-v0_8-whitegrid')
except OSError:
    plt.rcParams['axes.grid'] = True

df = pd.read_csv(CSV_PATH)
if 'schedule' not in df.columns:
    raise SystemExit(f"missing 'schedule' column in {CSV_PATH}")

SCHEDULE_ORDER = ['static', 'static-1', 'dynamic-1', 'dynamic-4', 'guided-1']
mean_df = df.groupby(['workload', 'schedule'])['t_join'].mean().reset_index()

workloads = ['uniform', 'skewed']

fig, axes = plt.subplots(1, 2, figsize=(8, 3.2), sharey=False)

for ax, wl in zip(axes, workloads):
    sub = mean_df[mean_df['workload'] == wl]
    if sub.empty:
        continue
    sub = sub.set_index('schedule').reindex(SCHEDULE_ORDER).reset_index()
    schedules = sub['schedule'].values
    times_ms = sub['t_join'].values * 1000.0
    x = np.arange(len(schedules))

    ax.bar(x, times_ms, width=0.65, color='C0', edgecolor='white', linewidth=0.5)
    ymax = float(times_ms.max())
    for xi, v in zip(x, times_ms):
        ax.text(xi, v + 0.012 * ymax, f"{v:.1f}", ha='center', va='bottom', fontsize=8)

    ax.set_title(wl.capitalize(), fontsize=10)
    ax.set_xlabel('Schedule policy', fontsize=10)
    ax.set_ylabel('Join time [ms]', fontsize=10)
    ax.tick_params(labelsize=9)
    ax.set_xticks(x)
    ax.set_xticklabels(schedules, fontsize=9, rotation=20, ha='right')
    ax.set_ylim(0, ymax * 1.15)

fig.suptitle("Join time vs schedule policy at $T=8$, $P=128$",
             fontsize=10, y=1.02)
fig.tight_layout()
os.makedirs(REPORT_DIR, exist_ok=True)
fig.savefig(os.path.join(REPORT_DIR, 'fig_schedule.pdf'), bbox_inches='tight')
fig.savefig(os.path.join(REPORT_DIR, 'fig_schedule.png'), dpi=150, bbox_inches='tight')
print("Saved fig_schedule.pdf and fig_schedule.png")
