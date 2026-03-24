import matplotlib.pyplot as plt
import pandas as pd
import numpy as np
import glob
import sys
import matplotlib.gridspec as gridspec

# --- 1. 寻找文件 ---
files = sorted(glob.glob("output/plt_HLLC_*.csv"))
if not files:
    print("Error: No csv files found!")
    sys.exit(1)

last_file = files[-1]
print(f"Plotting file: {last_file}")

# --- 2. 读取数据并排序 ---
df = pd.read_csv(last_file)

# 避免浮点数误差导致 pivot 失败，对坐标进行适度舍入 (保留5位小数)
df['x_round'] = df['x'].round(5)
df['y_round'] = df['y'].round(5)

# 动态检测组分列
species_cols = [col for col in df.columns if col.startswith('Y_')]

# 【核心修改】为了画出全域的平滑线，直接对整个 DataFrame 按 x 排序
df_sorted = df.sort_values(by='x')

# --- 3. 设置排版布局 (GridSpec 混合布局) ---
fig = plt.figure(figsize=(14, 14))

# 将画布分为 4 行 2 列：
# 第 0 行 (2列): 放 2D 云图
# 第 1, 2, 3 行 (横跨2列): 分别放密度、压力、组分的 1D 平滑曲线
gs = gridspec.GridSpec(4, 2, height_ratios=[1.5, 1, 1, 1], hspace=0.3)

# ==========================================
# 上半部分：2D 云图 (用于验证多维对称性)
# ==========================================
ax1 = fig.add_subplot(gs[0, 0])
grid_rho = df.pivot(index='y_round', columns='x_round', values='rho')
X, Y = np.meshgrid(grid_rho.columns, grid_rho.index)
c1 = ax1.contourf(X, Y, grid_rho.values, levels=50, cmap='viridis')
fig.colorbar(c1, ax=ax1, label='Density')
ax1.set_title("2D Density Contour")
ax1.set_xlabel("x")
ax1.set_ylabel("y")

ax2 = fig.add_subplot(gs[0, 1])
if 'v' in df.columns:
    grid_v = df.pivot(index='y_round', columns='x_round', values='u')
    v_max = max(abs(grid_v.values.max()), abs(grid_v.values.min()), 1e-10) 
    c2 = ax2.contourf(X, Y, grid_v.values, levels=50, cmap='RdBu_r', vmin=-v_max, vmax=v_max)
    fig.colorbar(c2, ax=ax2, label='V-Velocity')
    ax2.set_title(f"2D V-Velocity (Max: {grid_v.values.max():.2e})")
ax2.set_xlabel("x")
ax2.set_ylabel("y")

# ==========================================
# 下半部分：1D 平滑构图 (单轴、全计算域)
# ==========================================
# === 1. 密度 ===
ax3 = fig.add_subplot(gs[1, :])
ax3.plot(df_sorted['x'], df_sorted['rho'], 'k-', linewidth=2, label='Density')
ax3.set_title("Sod Shock Tube Result (1D Projection of Full 2D Domain)")
ax3.set_ylabel("Density")
ax3.grid(True, linestyle='--', alpha=0.6)
ax3.legend(loc='upper right')

# === 2. 压力 ===
# 共享 X 轴，使得拖动或缩放时能对齐
ax4 = fig.add_subplot(gs[2, :], sharex=ax3)
if 'p' in df.columns:
    ax4.plot(df_sorted['x'], df_sorted['p'], 'r-', linewidth=2, label='Pressure')
ax4.set_ylabel("Pressure")
ax4.grid(True, linestyle='--', alpha=0.6)
ax4.legend(loc='upper right')

# === 3. 组分分布 ===
ax5 = fig.add_subplot(gs[3, :], sharex=ax3)
colors = ['tab:blue', 'tab:orange', 'tab:green', 'tab:purple']
for i, col in enumerate(species_cols):
    color = colors[i % len(colors)]
    ax5.plot(df_sorted['x'], df_sorted[col], '-', linewidth=2, color=color, label=col, alpha=0.9)

ax5.set_title("Species Distribution (Contact Discontinuity)")
ax5.set_ylabel("Mass Fraction")
ax5.set_xlabel("Position (x)")
ax5.set_ylim(-0.05, 1.05)
ax5.grid(True, linestyle='--', alpha=0.6)
ax5.legend(loc='center right')

# --- 隐藏中间图表的 X 轴标签，使得堆叠更紧凑 ---
plt.setp(ax3.get_xticklabels(), visible=False)
plt.setp(ax4.get_xticklabels(), visible=False)

# --- 4. 保存与展示 ---
plt.tight_layout()
out_name = "sod_HLLC_planed.png"
plt.savefig(out_name, dpi=300)
print(f"Saved: {out_name}")
plt.show()