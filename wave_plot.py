import h5py
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import glob
import sys
import os

# ==============================================================================
# 1. 配置与文件寻找
# ==============================================================================
plt_files = sorted(glob.glob("output/*_plt_*.h5"))
chk_files = sorted(glob.glob("output/*_CDet_chk_*.h5"))

if plt_files:
    last_file = plt_files[-1]
elif chk_files:
    last_file = chk_files[-1]
else:
    print(f"Error: No HDF5 files found in output/!")
    sys.exit(1)

if len(sys.argv) > 1:
    last_file = sys.argv[1]

print(f"Plotting file: {last_file}")

# ==============================================================================
# 2. 从 HDF5 智能提取数据与元信息
# ==============================================================================
data_dict = {}
axes = {}

with h5py.File(last_file, 'r') as f:
    sim_time = f.attrs.get('time', 0.0)
    dim = f.attrs.get('dim', 1)
    
    raw_geom = f.attrs.get('geometry', 'cartesian')
    geometry = raw_geom.decode('utf-8') if isinstance(raw_geom, bytes) else str(raw_geom)
        
    print(f"--- MetaData ---")
    print(f"Time: {sim_time:.5e}, Dim: {dim}, Geometry: {geometry}")
    print(f"----------------")

    possible_axis_names = ['x', 'y', 'z', 'r', 'theta', 'phi', 'r_cy', 'z_cy', 'phi_cy']

    if 'Grid' in f and 'Data' in f:
        for key in f['Grid'].keys():
            axes[key] = np.array(f['Grid'][key])
        for key in f['Data'].keys():
            data_dict[key] = np.array(f['Data'][key])
    elif 'rho' in f:
        print("[Warning] Checkpoint file detected. Primitive variables might not be fully available.")
        data_dict['rho'] = np.array(f['rho'])
        if 'mom_x' in f:
            data_dict['u'] = np.array(f['mom_x']) / data_dict['rho']
        if 'eng' in f:
            data_dict['eng'] = np.array(f['eng'])
        if dim == 1:
            axes['x'] = np.arange(len(data_dict['rho']))
        else:
            axes['x'] = np.arange(len(data_dict['rho']))

if 'p' not in data_dict:
    print("[Warning] Pressure 'p' not found. Using Ideal Gas approximation (Gamma=1.4) for visualization.")
    if 'eng' in data_dict and 'u' in data_dict:
        e_kin = 0.5 * data_dict['rho'] * (data_dict['u']**2)
        e_int = data_dict['eng'] - e_kin
        data_dict['p'] = e_int * (1.4 - 1.0)
    else:
        data_dict['p'] = np.zeros_like(data_dict['rho'])

if 'u' not in data_dict:
    data_dict['u'] = np.zeros_like(data_dict['rho'])

# ==============================================================================
# 3. 智能重构与切片引擎
# ==============================================================================
ordered_axes = [k for k in possible_axis_names if k in axes]
grid_shape = tuple([len(axes[k]) for k in ordered_axes][::-1])

for key in list(data_dict.keys()):
    if data_dict[key].size == np.prod(grid_shape):
        data_dict[key] = data_dict[key].reshape(grid_shape)

r_1d_sorted = None  
if len(ordered_axes) == 1:
    r_1d_sorted = axes[ordered_axes[0]]

# Extract all species keys
species_keys = [k for k in data_dict.keys() if k.startswith('X_')]

# ==============================================================================
# 4. 绘图与排版布局
# ==============================================================================
# Increase figure size to accommodate species plot
fig = plt.figure(figsize=(16, 18))
fig.suptitle(f"ARCH Simulation | Geometry: {geometry.capitalize()} | t = {sim_time:.5e}", fontsize=16)

if dim == 1:
    # --- 1D 专有布局 (加一张组分追踪图) ---
    gs = gridspec.GridSpec(3, 1, hspace=0.3)
    ax1 = fig.add_subplot(gs[0, 0])
    ax2 = fig.add_subplot(gs[1, 0], sharex=ax1)
    ax3 = fig.add_subplot(gs[2, 0], sharex=ax1)

    axis_arr = r_1d_sorted
    axis_name = ordered_axes[0] if ordered_axes else 'Index'

    # 1. 密度和速度
    ax1.plot(axis_arr, data_dict['rho'], 'c-', linewidth=2, label='Density')
    ax1.set_ylabel("Density")
    ax1.grid(True, linestyle='--', alpha=0.6)
    
    ax1_twin = ax1.twinx()
    ax1_twin.plot(axis_arr, data_dict['u'], 'r-', linewidth=1.5, alpha=0.8, label='Velocity')
    ax1_twin.set_ylabel("Velocity", color='r')
    ax1_twin.tick_params(axis='y', labelcolor='r')
    
    lines_1, labels_1 = ax1.get_legend_handles_labels()
    lines_2, labels_2 = ax1_twin.get_legend_handles_labels()
    ax1.legend(lines_1 + lines_2, labels_1 + labels_2, loc="upper right")

    # 2. 压力
    ax2.plot(axis_arr, data_dict['p'], 'b-', linewidth=2, label='Pressure')
    ax2.set_ylabel("Pressure")
    ax2.grid(True, linestyle='--', alpha=0.6)
    ax2.legend(loc="upper right")
    
    # 3. 全部组分 (对数坐标展示以验证微量元素的生成)
    for sp in species_keys:
        # 只画最大值大于 1e-10 的组分，避免图例太多
        if np.max(data_dict[sp]) > 1e-10:
            ax3.plot(axis_arr, data_dict[sp], linewidth=1.5, label=sp)
            
    ax3.set_yscale('log')
    ax3.set_ylim(bottom=1e-10, top=2.0)
    ax3.set_ylabel("Mass Fractions (Log Scale)")
    ax3.set_xlabel(f"Coordinate ({axis_name})")
    ax3.grid(True, linestyle='--', alpha=0.6)
    ax3.legend(loc="upper left", bbox_to_anchor=(1.01, 1), ncol=1)

else:
    print("2D/3D species plotting is not fully enabled in this snippet. Please stick to 1D ZND validation.")

# --- 保存与展示 ---
output_png = "table_check.png"
plt.tight_layout()
plt.savefig(output_png, dpi=300, bbox_inches='tight')
print(f"Saved: {output_png}")
