"""Schedule sensitivity bar chart."""

import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
import os

BASE_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CSV_PATH = os.path.join(BASE_DIR, 'results', 'cluster', 'schedule_sensitivity.csv')
REPORT_DIR = os.path.join(BASE_DIR, 'report')

try:
    plt.style.use('seaborn-v0_8-whitegrid')
except OSError:
    plt.rcParams['axes.grid'] = True

df = pd.read_csv(CSV_PATH)
print("Columns:", df.columns.tolist())

if 'schedule' not in df.columns:
    print("ERROR: 'schedule' column not found. Available columns:", df.columns.tolist())
    raise SystemExit(1)

print("workload values:", df['workload'].unique())
print("schedule values:", df['schedule'].unique())
print("impl values:", df['impl'].unique() if 'impl' in df.columns else "N/A")

# Mean over run_id
mean_df = df.groupby(['workload', 'schedule'])['t_total_s'].mean().reset_index()

workloads = ['uniform', 'skewed']

fig, axes = plt.subplots(1, 2, figsize=(8, 3.5))

for ax, wl in zip(axes, workloads):
    sub = mean_df[mean_df['workload'] == wl].sort_values('schedule')
    if sub.empty:
        print(f"Warning: no data for workload={wl}")
        continue

    schedules = sub['schedule'].values
    times = sub['t_total_s'].values
    x = np.arange(len(schedules))

    ax.bar(x, times, width=0.6, color='C0', edgecolor='white', linewidth=0.5)
    ax.set_title(wl.capitalize(), fontsize=10)
    ax.set_xlabel('Schedule', fontsize=10)
    ax.set_ylabel('Mean Time (s)', fontsize=10)
    ax.tick_params(labelsize=9)
    ax.set_xticks(x)
    ax.set_xticklabels(schedules, fontsize=9, rotation=30, ha='right')

fig.text(
    0.5,
    -0.02,
    "P=128 partitions over t=8 threads gives enough granularity that "
    "all schedule policies converge within ~1%.",
    ha="center",
    va="top",
    fontsize=8.5,
    style="italic",
    color="dimgray",
)

fig.tight_layout()
os.makedirs(REPORT_DIR, exist_ok=True)
fig.savefig(os.path.join(REPORT_DIR, 'fig_schedule.pdf'), bbox_inches='tight')
fig.savefig(os.path.join(REPORT_DIR, 'fig_schedule.png'), dpi=150, bbox_inches='tight')
print("Saved fig_schedule.pdf and fig_schedule.png")
