import h5py
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import glob
import sys
import os

# ==============================================================================
# 1. 配置与文件寻找
# ==============================================================================
FILE_PATTERN = "output/*_plt_*.h5"  
# FILE_PATTERN = "output/*_chk_*.h5" 

files = sorted(glob.glob(FILE_PATTERN))
if not files:
    print(f"Error: No HDF5 files found matching {FILE_PATTERN}!")
    sys.exit(1)

last_file = files[-1]
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
    print(f"Time: {sim_time:.5f}, Dim: {dim}, Geometry: {geometry}")
    print(f"----------------")

    # 定义优先寻找的坐标轴顺序 (内层循环 x/r 在前，外层循环 z/phi 在后)
    possible_axis_names = ['x', 'y', 'z', 'r', 'theta', 'phi', 'r_cy', 'z_cy', 'phi_cy']

    if 'Grid' in f and 'Data' in f:
        for key in f['Grid'].keys():
            axes[key] = np.array(f['Grid'][key])
            
        for key in f['Data'].keys():
            data_dict[key] = np.array(f['Data'][key])
            
    elif 'rho' in f:
        print("[Warning] Checkpoint file detected. Primitive variables not available.")
        data_dict['rho'] = np.array(f['rho'])
        data_dict['mom_x'] = np.array(f['mom_x'])
        data_dict['eng'] = np.array(f['eng'])

# ==============================================================================
# 3. 智能重构与切片引擎 (核心修改区：支持 3D 自动切片与 pcolormesh 极速渲染)
# ==============================================================================
# 按照 possible_axis_names 提取存在的坐标轴，并确保顺序正确 (对应 i, j, k)
ordered_axes = [k for k in possible_axis_names if k in axes]

# 计算 numpy 应当使用的 shape (由于 C++ 是 z-y-x 写入，我们用相反顺序 reshape)
grid_shape = tuple([len(axes[k]) for k in ordered_axes][::-1])

# 将所有一维压平的数据还原为 1D/2D/3D 矩阵
for key in list(data_dict.keys()):
    data_dict[key] = data_dict[key].reshape(grid_shape)

# 【核心功能】：如果是 3D 数据，自动提取 Z 轴的中间切片，降维成 2D 用于快速预览
if dim == 3:
    mid_z = grid_shape[0] // 2
    print(f"[3D Mode] Extracting mid-plane slice at index {mid_z} for fast 2D visualization.")
    for key in list(data_dict.keys()):
        data_dict[key] = data_dict[key][mid_z, :, :]
    ordered_axes = ordered_axes[:2] # 剥离最外层坐标，只保留二维绘图面

x_phys, y_phys = None, None
r_1d_sorted = None  

if len(ordered_axes) >= 2:
    # C1 是内层坐标 (x/r), C2 是外层坐标 (y/theta/phi)
    c1_name, c2_name = ordered_axes[0], ordered_axes[1]
    C1, C2 = np.meshgrid(axes[c1_name], axes[c2_name])
    r_1d_sorted = axes[c1_name]

    # --- 坐标系通用物理投影 (生成真正的 2D 绘图网格) ---
    if geometry == "cartesian":
        x_phys, y_phys = C1, C2
        
    elif geometry == "cylindrical":
        if c1_name == 'r_cy' and c2_name == 'z_cy':      # r-z 切面
            x_phys, y_phys = C1, C2
        elif c1_name == 'r_cy' and c2_name == 'phi_cy':  # r-phi 极坐标面
            x_phys = C1 * np.cos(C2)
            y_phys = C1 * np.sin(C2)
        else:
            x_phys, y_phys = C1, C2
            
    elif geometry == "spherical":
        if c1_name == 'r' and c2_name == 'theta':        # 侧视图
            x_phys = C1 * np.sin(C2)
            y_phys = C1 * np.cos(C2)
        elif c1_name == 'r' and c2_name == 'phi':        # 赤道俯视图
            x_phys = C1 * np.cos(C2)
            y_phys = C1 * np.sin(C2)
        else:
            x_phys, y_phys = C1, C2

elif len(ordered_axes) == 1:
    r_1d_sorted = axes[ordered_axes[0]]

# ==============================================================================
# 4. 绘图与排版布局
# ==============================================================================
fig = plt.figure(figsize=(16, 12))
fig.suptitle(f"ARCH Simulation | Geometry: {geometry.capitalize()} | t = {sim_time:.5f}", fontsize=16)

if (dim >= 2 or (dim == 3)) and x_phys is not None and y_phys is not None:
    gs = gridspec.GridSpec(3, 2, height_ratios=[2, 1, 1], hspace=0.3)

    # 1. 左上：物理空间云图 (Density) - 使用 pcolormesh 极大提升渲染速度
    ax1 = fig.add_subplot(gs[0, 0])
    # 注意：此时 data_dict['rho'] 已经是标准的 2D 矩阵了
    tp1 = ax1.pcolormesh(x_phys, y_phys, data_dict.get('rho', np.zeros_like(x_phys)), cmap='viridis', shading='auto')
    fig.colorbar(tp1, ax=ax1, label='Density')
    ax1.set_aspect('equal')
    ax1.set_title("Density Field (Physical Space)")
    ax1.set_xlabel(f"Physical X ({c1_name}-{c2_name} plane)")
    ax1.set_ylabel("Physical Y")

    # 2. 右上：物理空间云图 (Velocity)
    ax2 = fig.add_subplot(gs[0, 1])
    vel_data = data_dict.get('u', np.zeros_like(x_phys))
    tp2 = ax2.pcolormesh(x_phys, y_phys, vel_data, cmap='magma', shading='auto')
    fig.colorbar(tp2, ax=ax2, label='Velocity (Component 1)')
    ax2.set_aspect('equal')
    ax2.set_title("Velocity Field")

  # 3. 下方：保留完美平滑曲线 (无散点图)
    ax3 = fig.add_subplot(gs[1, :])
    ax4 = fig.add_subplot(gs[2, :], sharex=ax3)

    if geometry == "cylindrical" and 'r_cy' in axes and 'phi_cy' in axes:
        ax3.plot(axes['r_cy'], data_dict['rho'][0, :], 'c-', linewidth=2, label='1D Slice (\u03b8=0)')
        ax4.plot(axes['r_cy'], data_dict['p'][0, :], 'b-', linewidth=2, label='1D Slice (\u03b8=0)')
        ax4.set_xlabel("Radius / r_cy")
        
    elif geometry == "cartesian" and dim >= 2:
        # [新增逻辑]：直角坐标系下，提取中间的垂直切面 (Vertical Slice)
        # 注意：data_dict['rho'] 的 shape 是 (ny, nx)
        mid_x = grid_shape[1] // 2 
        x_val = axes['x'][mid_x]
        
        ax3.plot(axes['y'], data_dict['rho'][:, mid_x], 'c-', linewidth=2, label=f'Vertical Slice (x={x_val:.3f})')
        ax4.plot(axes['y'], data_dict['p'][:, mid_x], 'b-', linewidth=2, label=f'Vertical Slice (x={x_val:.3f})')
        ax4.set_xlabel("Physical Y")

    ax3.set_ylabel("Density")
    ax3.grid(True, linestyle='--', alpha=0.6)
    if ax3.get_legend_handles_labels()[0]:
        ax3.legend(loc="upper right")

    ax4.set_ylabel("Pressure")
    ax4.grid(True, linestyle='--', alpha=0.6)
    if ax4.get_legend_handles_labels()[0]:
        ax4.legend(loc="upper right")

# --- 保存与展示 ---
output_png = "arch_visualize.png"
plt.tight_layout()
plt.savefig(output_png, dpi=300)
print(f"Saved: {output_png}")
plt.show()