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
# 2. 从 HDF5 提取数据与元信息 (适配自研均匀网格 IO)
# ==============================================================================
data_dict = {}
axes = {}

with h5py.File(last_file, 'r') as f:
    sim_time = f.attrs.get('time', 0.0)
    dim = f.attrs.get('dim', 1)
    
    raw_geom = f.attrs.get('geometry', b'cartesian')
    geometry = raw_geom.decode('utf-8') if isinstance(raw_geom, bytes) else str(raw_geom)
        
    print(f"--- MetaData ---")
    print(f"Time: {sim_time:.5e}, Dim: {dim}, Geometry: {geometry}")
    print(f"----------------")

    # ----------------------------------------
    # 模式 A: 读取 PLT 文件 (推荐用于画图)
    # ----------------------------------------
    if 'Grid' in f and 'Data' in f:
        print("[Info] Detected PLT file format. Loading primitive variables and coordinates.")
        for key in f['Grid'].keys():
            axes[key] = np.array(f['Grid'][key])
        for key in f['Data'].keys():
            data_dict[key] = np.array(f['Data'][key])
            
    # ----------------------------------------
    # 模式 B: 读取 CHK 文件 (仅作回退/调试)
    # ----------------------------------------
    elif 'rho' in f:
        print("[Warning] Detected CHK file format. Calculating primitive variables from conservative states.")
        print("[Warning] Physical coordinates and species names are missing in CHK. Using dummy index & names.")
        
        rho = np.array(f['rho'])
        data_dict['rho'] = rho
        
        # 提取速度
        if 'mom_x' in f:
            mom_x = np.array(f['mom_x'])
            u = np.zeros_like(rho)
            mask = rho > 1e-12
            u[mask] = mom_x[mask] / rho[mask]
            data_dict['u'] = u
        else:
            data_dict['u'] = np.zeros_like(rho)
            
        # 提取压力 (假设 Gamma=1.4 的理想气体状态方程)
        if 'eng' in f:
            eng = np.array(f['eng'])
            data_dict['eng'] = eng
            e_kin = 0.5 * data_dict['rho'] * (data_dict['u']**2)
            e_int = eng - e_kin
            data_dict['p'] = e_int * (1.4 - 1.0)
        else:
            data_dict['p'] = np.zeros_like(rho)
            
        # CHK 不存坐标，用网格索引伪造 X 轴
        axes['x'] = np.arange(len(rho))
        
        # 提取并切分组分质量分数
        if 'mass_fractions' in f:
            mf = np.array(f['mass_fractions'])
            num_cells = len(rho)
            if mf.ndim == 1:
                # 判定一维数组中包含了几种组分
                num_species = len(mf) // num_cells
                try:
                    # 假设内存布局为 (num_species, num_cells) 的 SoA 展平结构
                    # 如果画出来的组分像白噪声，则改为 mf.reshape((num_cells, num_species)).T
                    mf_reshaped = mf.reshape((num_species, num_cells))
                    for i in range(num_species):
                        data_dict[f'X_{i}'] = mf_reshaped[i, :]
                except ValueError:
                    print("[Error] Dimension mismatch when reshaping mass_fractions.")
            elif mf.ndim == 2:
                # 若使用了二维数组存储
                if mf.shape[0] == num_cells:
                    for i in range(mf.shape[1]):
                        data_dict[f'X_{i}'] = mf[:, i]
                else:
                    for i in range(mf.shape[0]):
                        data_dict[f'X_{i}'] = mf[i, :]

    else:
        print(f"Error: Unknown HDF5 format in {last_file}!")
        sys.exit(1)

# ==============================================================================
# 3. 剥离多维处理，直接准备画图
# ==============================================================================
possible_axis_names = ['x', 'y', 'z', 'r', 'theta', 'phi', 'r_cy', 'z_cy', 'phi_cy']
ordered_axes = [k for k in possible_axis_names if k in axes]

# 提取用于绘图的坐标轴
r_1d_sorted = None
if ordered_axes:
    r_1d_sorted = axes[ordered_axes[0]]
else:
    r_1d_sorted = np.arange(len(data_dict['rho']))

# 提取组分 keys
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
output_png = "table_check1.png"
plt.tight_layout()
plt.savefig(output_png, dpi=300, bbox_inches='tight')
print(f"Saved: {output_png}")
