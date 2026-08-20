/**
 * @file Networks.h
 * @brief Compile-time network include surface for static duck typing.
 */
#pragma once

// Linear algebra backends shared by all networks.
#include "../linalg/DenseWrap.h"
#include "../linalg/SparseWrap.h"

// Nuclear reaction-network policies.
#include "../../physics/network/aprox13/NetAprox13.h"
#include "../../physics/network/aprox19/NetAprox19.h"
#include "../../physics/network/aprox21/NetAprox21.h"
#include "../../physics/network/iso7/NetIso7.h"

// NetType requirements are enforced by template instantiation.
