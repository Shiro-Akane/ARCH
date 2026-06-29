import numpy as np
import h5py
import os

# ==========================================
# 1. 物理参数定义 (严格对齐 .par 文件)
# ==========================================
# Species 0: Air
gamma_air = 1.4
cv_air    = 717.5

# Species 1: Helium
gamma_he  = 1.67
cv_he     = 3113.9

# ==========================================
# 2. 3D 网格参数设置
# ==========================================
n_rho = 150
n_e   = 150
n_X   = 50   # 质量分数维度，50个点对线性混合足够了

# 对数坐标范围 (覆盖 Sod 激波管的极端工况)
log_rho_min = -3.0   # 0.001 kg/m^3
log_rho_max =  1.0   # 10.0 kg/m^3
log_e_min   = -1.0   # 100 J/kg
log_e_max   =  2.0   # 1e7 J/kg
X_min       =  0.0   # 纯空气
X_max       =  1.0   # 纯氦气

# 生成 1D 坐标轴
log_rho_arr = np.linspace(log_rho_min, log_rho_max, n_rho)
log_e_arr   = np.linspace(log_e_min, log_e_max, n_e)
X_arr       = np.linspace(X_min, X_max, n_X)

# ==========================================
# 3. 初始化展平的 1D 数据数组 (C++ 兼容)
# ==========================================
total_size = n_rho * n_e * n_X
pressure    = np.zeros(total_size, dtype=np.float64)
temperature = np.zeros(total_size, dtype=np.float64)
sound_speed = np.zeros(total_size, dtype=np.float64)
dp_drho     = np.zeros(total_size, dtype=np.float64)
dp_de       = np.zeros(total_size, dtype=np.float64)

# ==========================================
# 4. 填充 3D 数据
# ==========================================
print("Generating 3D EOS Table for Air-Helium mixture...")

for i in range(n_rho):
    rho = 10.0**log_rho_arr[i]
    for j in range(n_e):
        e = 10.0**log_e_arr[j]
        for k in range(n_X):
            X_he = X_arr[k]
            X_air = 1.0 - X_he
            
            # --- 多组分混合热力学法则 (等价于 IdealGas.h) ---
            # 1. 混合定容比热 Cv_mix
            cv_mix = X_air * cv_air + X_he * cv_he
            
            # 2. 混合绝热指数 Gamma_mix
            num = X_air * cv_air * (gamma_air - 1.0) + X_he * cv_he * (gamma_he - 1.0)
            gamma_mix = 1.0 + (num / cv_mix)
            
            # 3. 状态计算
            p = (gamma_mix - 1.0) * rho * e
            t = e / cv_mix
            cs = np.sqrt(gamma_mix * p / rho)
            
            # --- C++ 宏对应的 1D 展平索引 ---
            # IDX(i, j, k) -> i * (n_e * n_X) + j * n_X + k
            idx = i * (n_e * n_X) + j * n_X + k
            
            pressure[idx]    = p
            temperature[idx] = t
            sound_speed[idx] = cs
            
            # 解析偏导数
            dp_drho[idx] = (gamma_mix - 1.0) * e
            dp_de[idx]   = (gamma_mix - 1.0) * rho

# ==========================================
# 5. 写入 HDF5 文件
# ==========================================
output_file = "ideal_gas_3d.h5"

with h5py.File(output_file, "w") as f:
    # 维度元数据
    f.create_dataset("n_rho", data=n_rho)
    f.create_dataset("n_e",   data=n_e)
    f.create_dataset("n_X",   data=n_X)
    
    # 边界元数据
    f.create_dataset("log_rho_min", data=log_rho_min)
    f.create_dataset("log_rho_max", data=log_rho_max)
    f.create_dataset("log_e_min",   data=log_e_min)
    f.create_dataset("log_e_max",   data=log_e_max)
    f.create_dataset("X_min",       data=X_min)
    f.create_dataset("X_max",       data=X_max)
    
    # 核心物理场
    f.create_dataset("pressure",    data=pressure)
    f.create_dataset("temperature", data=temperature)
    f.create_dataset("sound_speed", data=sound_speed)
    
    # 解析偏导数场
    f.create_dataset("dp_drho",     data=dp_drho)
    f.create_dataset("dp_de",       data=dp_de)

print(f"Successfully generated 3D Tabular EOS: {output_file}")
print(f"Total entries: {total_size} (Size: ~{total_size * 5 * 8 / 1024 / 1024:.2f} MB)")