#pragma once

#include "core/ArchPortability.h"

namespace arch::cuda {

// Bind the shared EOS's optional failure boundary without changing its type or
// instantiating a second physics graph. The backend owns this sticky 0/1 latch;
// on Device it must address global/shared memory and outlive every query in the
// launch/continuation. Binding a copy never mutates the persistent EOS owner.
template <class Eos>
ARCH_INLINE Eos bind_device_eos_status(Eos eos, int* status)
{
    if constexpr (requires(Eos view) { view.device_error_status; })
        eos.device_error_status = status;
    return eos;
}

} // namespace arch::cuda
