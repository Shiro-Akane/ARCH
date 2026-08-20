import numpy as np
import h5py
import os

# Species parameters must match the corresponding simulation configuration.
# Species 0: Air
gamma_air = 1.4
cv_air    = 717.5

# Species 1: Helium
gamma_he  = 1.67
cv_he     = 3113.9

# Three-dimensional table grid.
n_rho = 150
n_e   = 150
n_X   = 50   # Linear mixture interpolation is resolved by 50 composition points.

# Logarithmic bounds cover the intended Sod shock-tube states.
log_rho_min = -3.0   # 0.001 kg/m^3
log_rho_max =  1.0   # 10.0 kg/m^3
log_e_min   = -1.0   # 100 J/kg
log_e_max   =  2.0   # 1e7 J/kg
X_min       =  0.0   # Pure air.
X_max       =  1.0   # Pure helium.

# One-dimensional coordinate arrays.
log_rho_arr = np.linspace(log_rho_min, log_rho_max, n_rho)
log_e_arr   = np.linspace(log_e_min, log_e_max, n_e)
X_arr       = np.linspace(X_min, X_max, n_X)

# Flat arrays matching the C++ table layout.
total_size = n_rho * n_e * n_X
pressure    = np.zeros(total_size, dtype=np.float64)
temperature = np.zeros(total_size, dtype=np.float64)
sound_speed = np.zeros(total_size, dtype=np.float64)
dp_drho     = np.zeros(total_size, dtype=np.float64)
dp_de       = np.zeros(total_size, dtype=np.float64)

# Populate the three-dimensional table.
print("Generating 3D EOS Table for Air-Helium mixture...")

for i in range(n_rho):
    rho = 10.0**log_rho_arr[i]
    for j in range(n_e):
        e = 10.0**log_e_arr[j]
        for k in range(n_X):
            X_he = X_arr[k]
            X_air = 1.0 - X_he
            
            # Mixture thermodynamics equivalent to IdealGas.h.
            # Mass-weighted constant-volume heat capacity.
            cv_mix = X_air * cv_air + X_he * cv_he
            
            # Heat-capacity-weighted adiabatic index.
            num = X_air * cv_air * (gamma_air - 1.0) + X_he * cv_he * (gamma_he - 1.0)
            gamma_mix = 1.0 + (num / cv_mix)
            
            # Thermodynamic state.
            p = (gamma_mix - 1.0) * rho * e
            t = e / cv_mix
            cs = np.sqrt(gamma_mix * p / rho)
            
            # Flat index used by the C++ table implementation.
            # IDX(i, j, k) -> i * (n_e * n_X) + j * n_X + k
            idx = i * (n_e * n_X) + j * n_X + k
            
            pressure[idx]    = p
            temperature[idx] = t
            sound_speed[idx] = cs
            
            # Analytical pressure derivatives.
            dp_drho[idx] = (gamma_mix - 1.0) * e
            dp_de[idx]   = (gamma_mix - 1.0) * rho

# Write the HDF5 table.
output_file = "ideal_gas_3d.h5"

with h5py.File(output_file, "w") as f:
    # Grid dimensions.
    f.create_dataset("n_rho", data=n_rho)
    f.create_dataset("n_e",   data=n_e)
    f.create_dataset("n_X",   data=n_X)
    
    # Coordinate bounds.
    f.create_dataset("log_rho_min", data=log_rho_min)
    f.create_dataset("log_rho_max", data=log_rho_max)
    f.create_dataset("log_e_min",   data=log_e_min)
    f.create_dataset("log_e_max",   data=log_e_max)
    f.create_dataset("X_min",       data=X_min)
    f.create_dataset("X_max",       data=X_max)
    
    # Thermodynamic fields.
    f.create_dataset("pressure",    data=pressure)
    f.create_dataset("temperature", data=temperature)
    f.create_dataset("sound_speed", data=sound_speed)
    
    # Analytical derivative fields.
    f.create_dataset("dp_drho",     data=dp_drho)
    f.create_dataset("dp_de",       data=dp_de)

print(f"Successfully generated 3D Tabular EOS: {output_file}")
print(f"Total entries: {total_size} (Size: ~{total_size * 5 * 8 / 1024 / 1024:.2f} MB)")
