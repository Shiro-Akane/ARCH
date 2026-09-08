/** Registered CUDA execution bindings select the shared ODE policy types. */
#pragma once
#include "driver/dispatch/PolicyDescriptor.h"
#include "numerics/burnsolver/ode_be-nr.h"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/burnsolver/ode_ros4.h"
namespace arch::cuda::burn_detail {
template <class Binding>
struct OdeType;
template <>
struct OdeType<dispatch::CudaBeNrBinding> {
    template <class Network, class Matrix, class Linear>
    using solver = Solver_BE_NR<Network, Matrix, Linear>;
};
template <>
struct OdeType<dispatch::CudaBdBinding> {
    template <class Network, class Matrix, class Linear>
    using solver = Solver_BD<Network, Matrix, Linear>;
};
template <>
struct OdeType<dispatch::CudaRos4Binding> {
    template <class Network, class Matrix, class Linear>
    using solver = Solver_ROS4<Network, Matrix, Linear>;
};
} // namespace arch::cuda::burn_detail
