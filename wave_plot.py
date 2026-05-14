import h5py
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import glob
import sys
import os

# ==============================================================================
# 1. 配置与文件寻找 (支持 plt 或 chk)
# ==============================================================================
# 你可以选择寻找 plt 还是 chk 文件
FILE_PATTERN = "output/*_plt_*.h5"  # 修改为你的文件前缀，例如 "output/SodTube_plt_*.h5"
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
    # 提取全局属性 (Attribute)
    sim_time = f.attrs.get('time', 0.0)
    dim = f.attrs.get('dim', 1)
    
    # 安全提取 geometry (处理 bytes 和 str 的兼容性)
    raw_geom = f.attrs.get('geometry', 'cartesian')
    if isinstance(raw_geom, bytes):
        geometry = raw_geom.decode('utf-8')
    else:
        geometry = str(raw_geom)
        
    print(f"--- MetaData ---")
    print(f"Time: {sim_time:.5f}, Dim: {dim}, Geometry: {geometry}")
    print(f"----------------")

    # 定义可能的坐标轴名称列表 (对应你在 C++ Grid.h 里的 GetAxisNames)
    possible_axis_names = ['x', 'y', 'z', 'r', 'theta', 'phi', 'r_cy', 'z_cy', 'phi_cy']

    if 'Grid' in f and 'Data' in f:
        # 这是结构规范的 plt 文件
        grid_group = f['Grid']
        data_group = f['Data']
        
        # 提取网格坐标轴 (直接遍历 Grid 组，再也不用猜名字了)
        for key in grid_group.keys():
            axes[key] = np.array(grid_group[key])
            
        # 提取物理场数据 (直接遍历 Data 组)
        for key in data_group.keys():
            data_dict[key] = np.array(data_group[key]).flatten()
            
    elif 'rho' in f:
        # 这是 chk 文件，数据全在根目录
        print("[Warning] Checkpoint file detected. Primitive variables not available.")
        data_dict['rho'] = np.array(f['rho'])
        data_dict['mom_x'] = np.array(f['mom_x'])
        data_dict['eng'] = np.array(f['eng'])

# ==============================================================================
# 3. 智能坐标系转换引擎 (生成绘图用的 x_phys, y_phys)
# ==============================================================================
# 我们需要把 HDF5 中存的独立坐标轴 (例如 r 数组, theta 数组) 通过 meshgrid 变成 2D 网格，然后再展平，
# 这样才能和 flatten() 后的物理场数据（如 rho）一一对应。

x_phys = None
y_phys = None
r_1d_sorted = None  

if dim == 2:
    if geometry == "cartesian":
        # 默认 xy 索引：X 变化最快，匹配 C++ 的内层 i 循环
        X, Y = np.meshgrid(axes['x'], axes['y'], indexing='xy')
        x_phys = X.flatten()
        y_phys = Y.flatten()
        r_1d_sorted = x_phys 
        
    elif geometry == "cylindrical":
        if 'r_cy' in axes and 'z_cy' in axes:
            R, Z = np.meshgrid(axes['r_cy'], axes['z_cy'], indexing='xy')
            x_phys = R.flatten()
            y_phys = Z.flatten()
            r_1d_sorted = x_phys
        elif 'r_cy' in axes and 'phi_cy' in axes:
             # 柱坐标极坐标面
             R, Phi = np.meshgrid(axes['r_cy'], axes['phi_cy'], indexing='xy')
             x_phys = (R * np.cos(Phi)).flatten()
             y_phys = (R * np.sin(Phi)).flatten()
             r_1d_sorted = R.flatten()

    elif geometry == "spherical":
        if 'r' in axes and 'phi' in axes:
            R, Phi = np.meshgrid(axes['r'], axes['phi'], indexing='xy')
            x_phys = (R * np.cos(Phi)).flatten()
            y_phys = (R * np.sin(Phi)).flatten()
            r_1d_sorted = R.flatten()
        elif 'r' in axes and 'theta' in axes:
            R, Theta = np.meshgrid(axes['r'], axes['theta'], indexing='xy')
            x_phys = (R * np.sin(Theta)).flatten()  
            y_phys = (R * np.cos(Theta)).flatten()  
            r_1d_sorted = R.flatten()

elif dim == 1:
    axis_name = list(axes.keys())[0]
    r_1d_sorted = axes[axis_name]

# ==============================================================================
# 4. 绘图与排版布局
# ==============================================================================
fig = plt.figure(figsize=(16, 12))
fig.suptitle(f"ARCH Simulation | Geometry: {geometry.capitalize()} | t = {sim_time:.5f}", fontsize=16)

# --- 情况 A: 2D 模拟的渲染 ---
if dim >= 2 and x_phys is not None and y_phys is not None:
    gs = gridspec.GridSpec(3, 2, height_ratios=[2, 1, 1], hspace=0.3)

    # 1. 左上：还原后的 2D 物理空间云图 (Density)
    ax1 = fig.add_subplot(gs[0, 0])
    tp1 = ax1.tripcolor(x_phys, y_phys, data_dict.get('rho', []), cmap='viridis', shading='gouraud')
    fig.colorbar(tp1, ax=ax1, label='Density')
    ax1.set_aspect('equal')
    ax1.set_title("Density Field (Physical Space)")
    ax1.set_xlabel("X (Cartesian reconstructed)")
    ax1.set_ylabel("Y (Cartesian reconstructed)")

    # 2. 右上：还原后的 2D 速度矢量图 (Primary Velocity)
    ax2 = fig.add_subplot(gs[0, 1])
    vel_data = data_dict.get('u', np.zeros_like(x_phys)) # 如果没算 u，填 0
    tp2 = ax2.tripcolor(x_phys, y_phys, vel_data, cmap='magma', shading='gouraud')
    fig.colorbar(tp2, ax=ax2, label='Velocity (Component 1)')
    ax2.set_aspect('equal')
    ax2.set_title("Velocity Field")

   # 3. 准备 1D 径向分布图
    ax3 = fig.add_subplot(gs[1, :])
    ax4 = fig.add_subplot(gs[2, :], sharex=ax3)

    # 方案 A：展示所有点的密集散点图 (验证对称性)
    # 取消 stride，使用极小的 markersize 和透明度
    ax3.plot(r_1d_sorted, data_dict.get('rho', []), 'k.', markersize=0.5, alpha=0.1, label='All 2D Points')
    ax4.plot(r_1d_sorted, data_dict.get('p', []), 'r.', markersize=0.5, alpha=0.1, label='All 2D Points')

    # 方案 B：提取 theta=0 的一维平滑切面连线 (完美平滑曲线)
    # 因为 C++ 写入是按 [y][x] (即 [phi][r]) 顺序的
    if geometry == "cylindrical" and 'r_cy' in axes and 'phi_cy' in axes:
        nx = len(axes['r_cy'])
        ny = len(axes['phi_cy'])
        
        # 将 1D 数组重构回 2D，然后取第一行 (j=0, phi=0 的切面)
        r_slice = r_1d_sorted.reshape((ny, nx))[0, :]
        rho_slice = data_dict['rho'].reshape((ny, nx))[0, :]
        p_slice = data_dict['p'].reshape((ny, nx))[0, :]
        
        ax3.plot(r_slice, rho_slice, 'c-', linewidth=2, label='1D Slice (\u03b8=0)')
        ax4.plot(r_slice, p_slice, 'b-', linewidth=2, label='1D Slice (\u03b8=0)')

    ax3.set_ylabel("Density")
    ax3.grid(True, linestyle='--', alpha=0.6)
    ax3.legend(loc="upper right")

    ax4.set_ylabel("Pressure")
    ax4.set_xlabel("Radius (r)")
    ax4.grid(True, linestyle='--', alpha=0.6)
    ax4.legend(loc="upper right")

# --- 保存与展示 ---
output_png = "arch_visualize.png"
plt.tight_layout()
plt.savefig(output_png, dpi=300)
print(f"Saved: {output_png}")
plt.show() # 如果你在没有 GUI 的服务器上，注释掉这行