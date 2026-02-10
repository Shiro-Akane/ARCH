import matplotlib.pyplot as plt
import pandas as pd
import glob
import sys

# --- 1. 寻找文件 ---
files = sorted(glob.glob("output/plt_HLLC_*.csv"))
if not files:
    print("Error: No csv files found!")
    sys.exit(1)

# 画最后一个文件，看最终状态
last_file = files[-1]
print(f"Plotting file: {last_file}")

df = pd.read_csv(last_file)

# --- 2. 动态检测组分列 ---
# 找出所有以 "Y_" 开头的列名 (例如 Y_Helium, Y_Air)
species_cols = [col for col in df.columns if col.startswith('Y_')]

# --- 3. 设置画布 (增加到 3 行) ---
fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(10, 12), sharex=True)

# === Subplot 1: 密度 ===
ax1.plot(df['x'], df['rho'], 'k-', label='Density', linewidth=2)
ax1.set_ylabel("Density")
ax1.set_title(f"Sod Shock Tube Result (Solver: HLLC)")
ax1.grid(True, linestyle='--', alpha=0.6)
ax1.legend(loc='upper right')

# === Subplot 2: 压力 ===
if 'p' in df.columns:
    ax1_twin = ax1.twinx() # 也可以画在双轴上，但单独画更清晰，这里保持单独画
    ax2.plot(df['x'], df['p'], 'r-', label='Pressure', linewidth=2)
    ax2.set_ylabel("Pressure")
    ax2.grid(True, linestyle='--', alpha=0.6)
    ax2.legend(loc='upper right')

# === Subplot 3: 组分质量分数 (关键修改) ===
# 自动遍历画出所有组分
colors = ['tab:blue', 'tab:orange', 'tab:green', 'tab:purple']
for i, col in enumerate(species_cols):
    color = colors[i % len(colors)]
    ax3.plot(df['x'], df[col], label=col, linewidth=2, color=color, alpha=0.8)

ax3.set_ylabel("Mass Fraction")
ax3.set_xlabel("Position (x)")
ax3.set_ylim(-0.1, 1.1) # 质量分数应该在 0-1 之间，留点边距
ax3.grid(True, linestyle='--', alpha=0.6)
ax3.legend(loc='center right')
ax3.set_title("Species Distribution (Contact Discontinuity)")

# --- 4. 保存 ---
plt.tight_layout()
plt.savefig("sod_HLLC.png", dpi=300)
print("Saved: sod_HLLC.png")
plt.show()