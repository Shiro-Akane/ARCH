import numpy as np
import h5py
import os
import helmeos
from concurrent.futures import ProcessPoolExecutor
import multiprocessing

# ==========================================
# 1. 物理参数与网格设定 (保持不变)
# ==========================================
n_rho = 150
n_e   = 150
n_A   = 15  
n_Z   = 15  

log_rho_min = -3.0
log_rho_max = 10.0
log_e_min   = 12.0  
log_e_max   = 22.0  

A_min, A_max = 1.0, 56.0
Z_min, Z_max = 1.0, 26.0

log_rho_arr = np.linspace(log_rho_min, log_rho_max, n_rho)
log_e_arr   = np.linspace(log_e_min, log_e_max, n_e)
A_arr       = np.linspace(A_min, A_max, n_A)
Z_arr       = np.linspace(Z_min, Z_max, n_Z)

# ==========================================
# 2. 初始化 1D 展平数组 (保持不变)
# ==========================================
total_size = n_rho * n_e * n_A * n_Z
pressure    = np.zeros(total_size, dtype=np.float64)
temperature = np.zeros(total_size, dtype=np.float64)
sound_speed = np.zeros(total_size, dtype=np.float64)
dp_drho     = np.zeros(total_size, dtype=np.float64)
dp_de       = np.zeros(total_size, dtype=np.float64)

# ==========================================
# 3. 初始化 Helmholtz EOS 并填充数据
# ==========================================
print("Loading helm_table.dat and initializing Helmholtz EOS...")
# 使用正确的类名实例化
eos = helmeos.HelmTable(fn="helm_table.dat") 

print(f"Generating 4D Table: {n_rho}x{n_e}x{n_A}x{n_Z} (Total points: {total_size})")

# 1. 定义单层密度切片的处理函数 (Worker Function)
def process_density_slice(i):
    # 每个子进程需要拥有自己独立的 EOS 实例，避免底层 Fortran 内存冲突
    local_eos = helmeos.HelmTable(fn="helm_table.dat")
    
    rho = 10.0**log_rho_arr[i]
    print(f"Worker processing slice {i}/{n_rho} (rho = {rho:.2e})...")
    
    # 局部数组，用于存储这一个密度切片产生的所有数据
    slice_size = n_e * n_A * n_Z
    local_P  = np.zeros(slice_size, dtype=np.float64)
    local_T  = np.zeros(slice_size, dtype=np.float64)
    local_cs = np.zeros(slice_size, dtype=np.float64)
    local_dpdrho = np.zeros(slice_size, dtype=np.float64)
    local_dpde   = np.zeros(slice_size, dtype=np.float64)
    
    for k in range(n_A):
        A_bar = A_arr[k]
        for l in range(n_Z):
            Z_bar = Z_arr[l]
            T_guess = 1.0e4 
            
            for j in range(n_e):
                e_target = 10.0**log_e_arr[j]
                
                try:
                    T_actual = local_eos.eos_invert(dens=rho, abar=A_bar, zbar=Z_bar, 
                                                    var=e_target, var_name='e', t0=T_guess)
                    T_actual = max(T_actual, 1e3) 
                    T_guess = T_actual
                except Exception:
                    T_actual = T_guess
                
                state = local_eos.eos_DT(den=rho, temp=T_actual, abar=A_bar, zbar=Z_bar)
                
                # 局部的 1D 索引 (去掉了 i 的维度)
                local_idx = j * (n_A * n_Z) + k * n_Z + l
                
                local_P[local_idx]  = state['ptot']
                local_T[local_idx]  = T_actual
                local_cs[local_idx] = state['cs']
                
                cv = state['cv']     
                local_dpde[local_idx]   = state['dpt'] / cv
                local_dpdrho[local_idx] = state['dpd'] - state['dpt'] * (state['ded'] / cv)

    # 返回该切片的索引 i 和对应的局部数据
    return i, local_P, local_T, local_cs, local_dpdrho, local_dpde

# 2. 调度进程池执行
if __name__ == '__main__':
    # 获取可用核心数 (留一个给系统)
    num_cores = max(1, multiprocessing.cpu_count() - 1)
    print(f"Starting parallel generation using {num_cores} cores...")
    
    with ProcessPoolExecutor(max_workers=num_cores) as executor:
        # 并发执行所有的密度切片
        results = executor.map(process_density_slice, range(n_rho))
        
        # 收集结果并填入全局大数组
        for i, l_P, l_T, l_cs, l_dpdrho, l_dpde in results:
            start_idx = i * (n_e * n_A * n_Z)
            end_idx   = start_idx + (n_e * n_A * n_Z)
            
            pressure[start_idx:end_idx]    = l_P
            temperature[start_idx:end_idx] = l_T
            sound_speed[start_idx:end_idx] = l_cs
            dp_drho[start_idx:end_idx]     = l_dpdrho
            dp_de[start_idx:end_idx]       = l_dpde

    print("Parallel computation finished. Writing to HDF5...")

# ==========================================
# 4. 写入 HDF5 
# ==========================================
output_filename = "white_dwarf_eos_4d.h5"
if os.path.exists(output_filename):
    os.remove(output_filename)

with h5py.File(output_filename, "w") as f:
    # 写入维度
    f.create_dataset("n_rho", data=n_rho)
    f.create_dataset("n_e",   data=n_e)
    f.create_dataset("n_A",   data=n_A)
    f.create_dataset("n_Z",   data=n_Z)
    
    # 写入边界
    f.create_dataset("log_rho_min", data=log_rho_min)
    f.create_dataset("log_rho_max", data=log_rho_max)
    f.create_dataset("log_e_min",   data=log_e_min)
    f.create_dataset("log_e_max",   data=log_e_max)
    f.create_dataset("A_min",       data=A_min)
    f.create_dataset("A_max",       data=A_max)
    f.create_dataset("Z_min",       data=Z_min)
    f.create_dataset("Z_max",       data=Z_max)
    
    # 写入物理场
    f.create_dataset("pressure",    data=pressure)
    f.create_dataset("temperature", data=temperature)
    f.create_dataset("sound_speed", data=sound_speed)
    f.create_dataset("dp_drho",     data=dp_drho)
    f.create_dataset("dp_de",       data=dp_de)

print(f"\nSuccessfully saved to {output_filename}")