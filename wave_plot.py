import matplotlib.pyplot as plt
import pandas as pd
import numpy as np
import glob
import sys
import matplotlib.gridspec as gridspec

# --- 1. 寻找并读取文件 ---
files = sorted(glob.glob("output/plt_HLLC_*.csv"))
if not files:
    print("Error: No csv files found!")
    sys.exit(1)

last_file = files[-1]
print(f"Plotting file: {last_file}")
df = pd.read_csv(last_file)

# 动态检测组分列
species_cols = [col for col in df.columns if col.startswith('Y_')]

# ==========================================
# 【核心逻辑】：从极坐标 (r, theta) 还原到笛卡尔 (x, y)
# ==========================================
# 假设输出文件中：
# 'x' 列实际上是半径 r
# 'y' 列实际上是极角 theta (弧度)
r = df['x'].values
theta = df['y'].values

# 进行物理坐标变换
# 如果你模拟的是全圆，theta 范围是 [0, 2pi]；如果是象限，则是 [0, pi/2]
x_phys = r * np.cos(theta)
y_phys = r * np.sin(theta)

# 将变换后的坐标存回 df 用于后续 1D 投影
df['r_phys'] = r  
df_sorted = df.sort_values(by='r_phys')

# ==========================================
# --- 3. 设置排版布局 ---
# ==========================================
fig = plt.figure(figsize=(16, 12))
gs = gridspec.GridSpec(3, 2, height_ratios=[2, 1, 1], hspace=0.3)

# ------------------------------------------
# 左上：还原后的 2D 物理空间云图 (Density)
# ------------------------------------------
ax1 = fig.add_subplot(gs[0, 0])
# 使用 tripcolor 处理非规则网格
tp1 = ax1.tripcolor(x_phys, y_phys, df['rho'], cmap='viridis', shading='gouraud')
fig.colorbar(tp1, ax=ax1, label='Density')
ax1.set_aspect('equal')
ax1.set_title("Physical Space: Cartesian Reconstruction (Density)")
ax1.set_xlabel("Physical X")
ax1.set_ylabel("Physical Y")

# ------------------------------------------
# 右上：还原后的 2D 速度矢量图 (Radial Velocity)
# ------------------------------------------
ax2 = fig.add_subplot(gs[0, 1])
# 注意：在球坐标下，u 是径向速度。还原到 2D 后，我们可以看它的强度。
tp2 = ax2.tripcolor(x_phys, y_phys, df['u'], cmap='magma', shading='gouraud')
fig.colorbar(tp2, ax=ax2, label='Radial Velocity (Ur)')
ax2.set_aspect('equal')
ax2.set_title("Physical Space: Radial Velocity Field")
ax2.set_xlabel("Physical X")
ax2.set_ylabel("Physical Y")

# ------------------------------------------
# 下半部分：1D 径向分布 (用来判断物理准确度)
# ------------------------------------------
# 密度 1D
ax3 = fig.add_subplot(gs[1, :])
ax3.plot(df_sorted['r_phys'], df_sorted['rho'], 'k-', linewidth=2, label='Spherical 1D Result')
ax3.set_ylabel("Density")
ax3.set_title("Radial Profile (Consistency Check)")
ax3.grid(True, linestyle='--', alpha=0.6)

# 压力 1D
ax4 = fig.add_subplot(gs[2, :], sharex=ax3)
ax4.plot(df_sorted['r_phys'], df_sorted['p'], 'r-', linewidth=2, label='Spherical 1D Result')
ax4.set_ylabel("Pressure")
ax4.set_xlabel("Radius (r)")
ax4.grid(True, linestyle='--', alpha=0.6)

# --- 保存与展示 ---
plt.tight_layout()
plt.savefig("spherical_to_cartesian_check.png", dpi=300)
print("Saved: spherical_to_cartesian_check.png")
plt.show()