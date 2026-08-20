from concurrent.futures import ProcessPoolExecutor
import multiprocessing
import os
from pathlib import Path

import h5py
import helmeos
import numpy as np

# Resolve the maintained Timmes table independently of the process working directory.
helm_table_path = (
    Path(__file__).resolve().parent
    / "eos_tabular"
    / "helmholtz"
    / "helm_table.dat"
)

# Thermodynamic table dimensions and coordinate bounds.
n_rho = 150
n_T   = 150
n_A   = 15  
n_Z   = 15  

log_rho_min = -3.0
log_rho_max = 10.0
log_T_min   = 4.0  
log_T_max   = 11.0  

A_min, A_max = 1.0, 56.0
Z_min, Z_max = 1.0, 26.0

log_rho_arr = np.linspace(log_rho_min, log_rho_max, n_rho)
log_T_arr   = np.linspace(log_T_min, log_T_max, n_T)
A_arr       = np.linspace(A_min, A_max, n_A)
Z_arr       = np.linspace(Z_min, Z_max, n_Z)

# Flat output arrays matching the C++ table layout.
total_size = n_rho * n_T * n_A * n_Z
pressure    = np.zeros(total_size, dtype=np.float64)
energy      = np.zeros(total_size, dtype=np.float64)
sound_speed = np.zeros(total_size, dtype=np.float64)
cv          = np.zeros(total_size, dtype=np.float64)
dp_drho     = np.zeros(total_size, dtype=np.float64)
dp_dT       = np.zeros(total_size, dtype=np.float64)

# Initialize the Helmholtz EOS and populate the table.
print(f"Loading {helm_table_path} and initializing Helmholtz EOS...")
# helmeos exposes the Timmes table through HelmTable.
eos = helmeos.HelmTable(fn=str(helm_table_path))

print(f"Generating 4D Table: {n_rho}x{n_T}x{n_A}x{n_Z} (Total points: {total_size})")

# Process one density plane per worker.
def process_density_slice(i):
    # Each process owns an EOS instance because the Fortran state is not shared safely.
    local_eos = helmeos.HelmTable(fn=str(helm_table_path))
    
    rho = 10.0**log_rho_arr[i]
    print(f"Worker processing slice {i}/{n_rho} (rho = {rho:.2e})...")
    
    # Local arrays hold one complete density plane.
    slice_size = n_T * n_A * n_Z
    local_P  = np.zeros(slice_size, dtype=np.float64)
    local_E  = np.zeros(slice_size, dtype=np.float64)
    local_cs = np.zeros(slice_size, dtype=np.float64)
    local_cv = np.zeros(slice_size, dtype=np.float64)
    local_dpdrho = np.zeros(slice_size, dtype=np.float64)
    local_dpdT   = np.zeros(slice_size, dtype=np.float64)
    
    for k in range(n_A):
        A_bar = A_arr[k]
        for l in range(n_Z):
            Z_bar = Z_arr[l]
            T_guess = 1.0e4 
            
            for j in range(n_T):
                T_target = 10.0**log_T_arr[j]
                
                try:
                    state = local_eos.eos_DT(den=rho, temp=T_target, abar=A_bar, zbar=Z_bar)
                except Exception:
                    print(f"Error at rho={rho}, T={T_target}, A={A_bar}, Z={Z_bar}")
                    continue
                
                # Flatten the (T, A, Z) indices within this density plane.
                local_idx = j * (n_A * n_Z) + k * n_Z + l
                
                local_P[local_idx]  = state['ptot']
                local_E[local_idx]  = state['etot']
                local_cs[local_idx] = state['cs']
                local_cv[local_idx] = state['cv']
                local_dpdT[local_idx]   = state['dpt']
                local_dpdrho[local_idx] = state['dpd']

    # Return the plane index with its local fields.
    return i, local_P, local_E, local_cs, local_cv, local_dpdrho, local_dpdT

# Dispatch density planes to the process pool.
if __name__ == '__main__':
    # Leave one logical CPU available for the operating system.
    num_cores = max(1, multiprocessing.cpu_count() - 1)
    print(f"Starting parallel generation using {num_cores} cores...")
    
    with ProcessPoolExecutor(max_workers=num_cores) as executor:
        # Evaluate every density plane concurrently.
        results = executor.map(process_density_slice, range(n_rho))
        
        # Copy completed planes into the global flat arrays.
        for i, l_P, l_E, l_cs, l_cv, l_dpdrho, l_dpdT in results:
            start_idx = i * (n_T * n_A * n_Z)
            end_idx   = start_idx + (n_T * n_A * n_Z)
            
            pressure[start_idx:end_idx]    = l_P
            energy[start_idx:end_idx]      = l_E
            sound_speed[start_idx:end_idx] = l_cs
            cv[start_idx:end_idx]          = l_cv
            dp_drho[start_idx:end_idx]     = l_dpdrho
            dp_dT[start_idx:end_idx]       = l_dpdT

    print("Parallel computation finished. Writing to HDF5...")

# Write the HDF5 table consumed by Tabular4DEOS.
output_filename = "white_dwarf_eos_4d.h5"
if os.path.exists(output_filename):
    os.remove(output_filename)

with h5py.File(output_filename, "w") as f:
    # Grid dimensions.
    f.create_dataset("n_rho", data=n_rho)
    f.create_dataset("n_T",   data=n_T)
    f.create_dataset("n_A",   data=n_A)
    f.create_dataset("n_Z",   data=n_Z)
    
    # Coordinate bounds.
    f.create_dataset("log_rho_min", data=log_rho_min)
    f.create_dataset("log_rho_max", data=log_rho_max)
    f.create_dataset("log_T_min",   data=log_T_min)
    f.create_dataset("log_T_max",   data=log_T_max)
    f.create_dataset("A_min",       data=A_min)
    f.create_dataset("A_max",       data=A_max)
    f.create_dataset("Z_min",       data=Z_min)
    f.create_dataset("Z_max",       data=Z_max)
    
    # Thermodynamic fields.
    f.create_dataset("pressure",    data=pressure)
    f.create_dataset("energy",      data=energy)
    f.create_dataset("sound_speed", data=sound_speed)
    f.create_dataset("cv",          data=cv)
    f.create_dataset("dp_drho",     data=dp_drho)
    f.create_dataset("dp_dT",       data=dp_dT)

print(f"\nSuccessfully saved to {output_filename}")
