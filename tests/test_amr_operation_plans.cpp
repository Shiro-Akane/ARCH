#include "amr/AmrTransferPlans.h"

#include <type_traits>

static_assert(std::is_trivially_copyable_v<amr::LogicalAmrBox>);
static_assert(std::is_standard_layout_v<amr::AmrTransferOperation>);

int main()
{
    return 0;
}
