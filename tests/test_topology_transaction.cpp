#include "amr/TopologyTransaction.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<amr::TopologyTransaction>);
static_assert(!std::is_copy_assignable_v<amr::TopologyTransaction>);

int main()
{
    return 0;
}
