/**
 * @file CudaBackendGravity.cpp
 * @brief Connect the CUDA backend state and published patch fields to the gravity service.
 *
 * Workflow:
 * 1. Use the selected CUDA device, stream and backend state lease.
 * 2. Connect the CUDA backend state and published patch fields to the gravity service.
 * 3. Publish or retire resident results only after the required stream ordering.
 */

#include "cuda/runtime/control/CudaBackendInternal.h"
#include "cuda/runtime/gravity/CudaGravityExecution.h"

namespace arch::cuda {
/** Provide one stream-bound CUDA execution owner for composite gravity. */
std::shared_ptr<Physical::Gravity::GravityExecution> CudaBackend::gravity_execution() {
    auto execution=impl_->gravity_execution.lock();
    if(!execution){execution=make_cuda_gravity_execution(impl_->stream.get(),impl_->device_ordinal,impl_);impl_->gravity_execution=execution;}
    return execution;
}
/** Borrow the device-resident density of the requested state generation. */
const double* CudaBackend::gravity_density(backend::BackendStateAccess access) {
    return impl_->require_block(access).require_access(access).rho;
}
/** Publish resident face acceleration to CUDA hydro kernels. */
void CudaBackend::publish_gravity(backend::BackendStateAccess access,Physical::Gravity::GravityPatchView view) {
    auto& block=impl_->require_block(access);
    if(view.density!=block.require_access(access).rho||!view.faces[0])throw std::logic_error("Gravity publication has stale device density");
    block.self_gravity=view;block.gravity_generation=gravity_generation_;gravity_ready_=true;
}
/** Clear stale device gravity publication before a new solve or regrid. */
void CudaBackend::invalidate_gravity() {
    // A new solve publishes every active block before any consumer is called.
    // Stamps are cleared through the accessed active store, including regrids.
    gravity_ready_=false;
    if(++gravity_generation_==0)throw std::overflow_error("CUDA gravity generation exhausted");
}
}
