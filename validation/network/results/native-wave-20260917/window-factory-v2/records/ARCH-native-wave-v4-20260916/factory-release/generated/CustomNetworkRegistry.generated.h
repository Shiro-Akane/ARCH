#pragma once
#define ARCH_CUSTOM_NETWORK_COUNT 2
#define ARCH_CUSTOM_CPU_ONLY_NETWORK_COUNT 0
#define ARCH_CUSTOM_CUDA_NETWORK_COUNT 2
#define ARCH_FOR_EACH_CUSTOM_NETWORK(M) \
    M(Custom_audit150, 1024, "custom:audit150", NetCustom_audit150) \
    M(Custom_audit200, 1025, "custom:audit200", NetCustom_audit200)
#define ARCH_FOR_EACH_CPU_ONLY_CUSTOM_NETWORK(M)
#define ARCH_FOR_EACH_CUDA_CUSTOM_NETWORK(M) \
    M(Custom_audit150, 1024, "custom:audit150", NetCustom_audit150) \
    M(Custom_audit200, 1025, "custom:audit200", NetCustom_audit200)
#define ARCH_FOR_EACH_CUSTOM_NETWORK_LAYOUT(M) \
    M(Custom_audit150, 150, 0) \
    M(Custom_audit200, 200, 0)
#define ARCH_FOR_EACH_CUSTOM_NETWORK_NSE(M) \
    M(Custom_audit150, false, "unreliable_spin_data,screening_model_not_supported,weak_rates_present,extra_conserved_quantities,rates_not_certified_detailed_balance") \
    M(Custom_audit200, false, "unreliable_spin_data,screening_model_not_supported,weak_rates_present,extra_conserved_quantities,rates_not_certified_detailed_balance")
