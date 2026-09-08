/**
 * @file test_diffusion_rkl_parity.cu
 * @brief Compare CPU/CUDA diffusion operators and RKL stage updates.
 *
 * Check directional timestep limits, EOS failures, state-buffer preparation
 * and every stage against the shared host calculation and reference cases.
 */
#include <cuda_runtime.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "cuda/diffusion/DiffusionKernels.cuh"
#include "numerics/diffusion/DiffFlux.h"
#include "numerics/diffusion/DiffFunction.h"
#include "numerics/diffusion/DiffusionAMRStages.h"
#include "physics/diffusionCoe/diffusion_math.hpp"
#include "physics/eos/IdealGas.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"

namespace {

int failures = 0;

struct ErrorEnvelope
{
    double maximum_absolute = 0.0;
    double maximum_relative = 0.0;
    int absolute_cell = -1;
    int relative_cell = -1;
};

struct NumericBudget
{
    double absolute;
    double relative;
    bool exact;
};

struct FrozenRklCell
{
    std::uint64_t rho;
    std::uint64_t mom_u;
    std::uint64_t mom_v;
    std::uint64_t mom_w;
    std::uint64_t eng;
    std::uint64_t species0;
    std::uint64_t species1;
    std::uint64_t enuc;
};

static constexpr FrozenRklCell kFirst[] = {
    {0x3ff1a5738315afd1ULL, 0x3fcc41f9c9aae565ULL, 0xbfbbfb3dc140df1dULL, 0x3fabfb3dc140df1dULL, 0x402a5c84c305dbf5ULL, 0x3fd665fc68366d89ULL, 0x3fe4c666abac4496ULL, 0x40fe242000000000ULL},
    {0x3ff1a92dbb773716ULL, 0x3fcc560e1ce2df96ULL, 0xbfbbee6259af3316ULL, 0x3fabee6259af3316ULL, 0x402a72fdeb427f08ULL, 0x3fd6681e1a378761ULL, 0x3fe4c557302271c9ULL, 0x40fe243000000000ULL},
    {0x3ff1ace7f3d8be5cULL, 0x3fcc6a2864638704ULL, 0xbfbbe17f01bca016ULL, 0x3fabe17f01bca016ULL, 0x402a897d84830a51ULL, 0x3fd66a3fcc089534ULL, 0x3fe4c447b41d40e1ULL, 0x40fe244000000000ULL},
    {0x3ff1b0a22c3a45a2ULL, 0x3fcc7e48a02cdbadULL, 0xbfbbd493b969261aULL, 0x3fabd493b969261aULL, 0x402aa0038e562546ULL, 0x3fd66c617da9b55eULL, 0x3fe4c338379cffd7ULL, 0x40fe245000000000ULL},
    {0x3ff1b45c649bcce7ULL, 0x3fcc926ed03edd93ULL, 0xbfbbc7a080b4c522ULL, 0x3fabc7a080b4c522ULL, 0x402ab690084a776aULL, 0x3fd66e832f1b062dULL, 0x3fe4c228baa1fc68ULL, 0x40fe246000000000ULL},
    {0x3ff1b8169cfd542cULL, 0x3fcca69af4998cb4ULL, 0xbfbbbaa5579f7d30ULL, 0x3fabbaa5579f7d30ULL, 0x402acd22f1eea834ULL, 0x3fd670a4e05ca5c5ULL, 0x3fe4c1193d2c840bULL, 0x40fe247000000000ULL},
    {0x3ff1bbd0d55edb72ULL, 0x3fccbacd0d3ce911ULL, 0xbfbbada23e294e43ULL, 0x3fabada23e294e43ULL, 0x402ae3bc4ad15f25ULL, 0x3fd672c6916eb23cULL, 0x3fe4c009bf3ce3f9ULL, 0x40fe248000000000ULL},
    {0x3ff1bf8b0dc062b7ULL, 0x3fcccf051a28f2aaULL, 0xbfbba0973452385cULL, 0x3faba0973452385cULL, 0x402afa5c128143bbULL, 0x3fd674e84251498dULL, 0x3fe4befa40d36929ULL, 0x40fe249000000000ULL},
    {0x3ff1c3454621e9fdULL, 0x3fcce3431b5da980ULL, 0xbfbb93843a1a3b7bULL, 0x3fab93843a1a3b7bULL, 0x402b1102488cfd6fULL, 0x3fd67709f3048997ULL, 0x3fe4bdeac1f06050ULL, 0x40fe24a000000000ULL},
    {0x3ff1c6ff7e837142ULL, 0x3fccf78710db0d92ULL, 0xbfbb86694f81579eULL, 0x3fab86694f81579eULL, 0x402b27aeec8333c0ULL, 0x3fd6792ba388901fULL, 0x3fe4bcdb429415e4ULL, 0x40fe24b000000000ULL},
    {0x3ff1cab9b6e4f888ULL, 0x3fcd0bd0faa11edfULL, 0xbfbb794674878cc6ULL, 0x3fab794674878cc6ULL, 0x402b3e61fdf28e29ULL, 0x3fd67b4d53dd7ad7ULL, 0x3fe4bbcbc2bed61aULL, 0x40fe24c000000000ULL},
    {0x3ff1ce73ef467fcdULL, 0x3fcd2020d8afdd69ULL, 0xbfbb6c1ba92cdaf4ULL, 0x3fab6c1ba92cdaf4ULL, 0x402b551b7c69b42aULL, 0x3fd67d6f04036752ULL, 0x3fe4babc4270ece9ULL, 0x40fe24d000000000ULL},
    {0x3ff1d22e27a80712ULL, 0x3fcd3476ab07492eULL, 0xbfbb5ee8ed714226ULL, 0x3fab5ee8ed714226ULL, 0x402b6bdb67774d3eULL, 0x3fd67f90b3fa730eULL, 0x3fe4b9acc1aaa609ULL, 0x40fe24e000000000ULL},
    {0x3ff1d5e860098e58ULL, 0x3fcd48d271a76230ULL, 0xbfbb51ae4154c25eULL, 0x3fab51ae4154c25eULL, 0x402b82a1beaa00e2ULL, 0x3fd681b263c2bb6dULL, 0x3fe4b89d406c4cedULL, 0x40fe24f000000000ULL},
    {0x3ff1d9a2986b159dULL, 0x3fcd5d342c90286eULL, 0xbfbb446ba4d75b9cULL, 0x3fab446ba4d75b9cULL, 0x402b996e81907695ULL, 0x3fd683d4135c5dbdULL, 0x3fe4b78dbeb62cd1ULL, 0x40fe250000000000ULL},
    {0x3ff1dd5cd0cc9ce3ULL, 0x3fcd719bdbc19be9ULL, 0xbfbb372117f90de0ULL, 0x3fab372117f90de0ULL, 0x402bb041afb955d1ULL, 0x3fd685f5c2c77731ULL, 0x3fe4b67e3c8890adULL, 0x40fe251000000000ULL},
};
static constexpr FrozenRklCell kScaledRkl1[] = {
    {0x3ff31aea0d97090eULL, 0x3fca795eb182fe61ULL, 0xbfbabe25ce1b6a28ULL, 0x3fab7db410c9e326ULL, 0x402b2c57ab33a842ULL, 0x3fd64b60998adc6aULL, 0x3fe4ae18e73ad58cULL, 0x40fe242000000000ULL},
    {0x3ff31eed87873083ULL, 0x3fca8c720091c5a9ULL, 0xbfbab1a22bdc224bULL, 0x3fab70d8a938371fULL, 0x402b437d6baf8daaULL, 0x3fd64d84bd7f1616ULL, 0x3fe4ad0feb39471bULL, 0x40fe243000000000ULL},
    {0x3ff322f1017757f7ULL, 0x3fca9f8af7b264b8ULL, 0xbfbaa516cf6e525cULL, 0x3fab63f55145a41fULL, 0x402b5aa9cea7cb35ULL, 0x3fd64fa8e0374e93ULL, 0x3fe4ac06ec060d38ULL, 0x40fe244000000000ULL},
    {0x3ff326f47b677f6dULL, 0x3fcab2a996e4db8cULL, 0xbfba9883b8d1fa59ULL, 0x3fab570a08f22a23ULL, 0x402b71dcd3a7a1dbULL, 0x3fd651cd01b44c8aULL, 0x3fe4aafde9a329f4ULL, 0x40fe245000000000ULL},
    {0x3ff32af7f557a6e2ULL, 0x3fcac5cdde292a26ULL, 0xbfba8be8e8071a43ULL, 0x3fab4a16d03dc92bULL, 0x402b89167a3a529dULL, 0x3fd653f121f6d5ffULL, 0x3fe4a9f4e4129dadULL, 0x40fe246000000000ULL},
    {0x3ff32efb6f47ce56ULL, 0x3fcad8f7cd7f5085ULL, 0xbfba7f465d0db219ULL, 0x3fab3d1ba7288139ULL, 0x402ba056c1eb1e79ULL, 0x3fd6561540ffb054ULL, 0x3fe4a8ebdb566719ULL, 0x40fe247000000000ULL},
    {0x3ff332fee937f5caULL, 0x3fcaec2764e74eabULL, 0xbfba729c17e5c1deULL, 0x3fab30188db2524cULL, 0x402bb79daa45466cULL, 0x3fd658395ecfa03eULL, 0x3fe4a7e2cf70833fULL, 0x40fe248000000000ULL},
    {0x3ff3370263281d3eULL, 0x3fcaff5ca4612495ULL, 0xbfba65ea188f498fULL, 0x3fab230d83db3c65ULL, 0x402bceeb32d40b77ULL, 0x3fd65a5d7b6769d9ULL, 0x3fe4a6d9c062ed83ULL, 0x40fe249000000000ULL},
    {0x3ff33b05dd1844b5ULL, 0x3fcb12978becd246ULL, 0xbfba59305f0a492fULL, 0x3fab15fa89a33f84ULL, 0x402be63f5b22ae94ULL, 0x3fd65c8196c7d092ULL, 0x3fe4a5d0ae2f9f96ULL, 0x40fe24a000000000ULL},
    {0x3ff33f0957086c28ULL, 0x3fcb25d81b8a57bfULL, 0xbfba4c6eeb56c0bbULL, 0x3fab08df9f0a5ba7ULL, 0x402bfd9a22bc70c4ULL, 0x3fd65ea5b0f19739ULL, 0x3fe4a4c798d8918fULL, 0x40fe24b000000000ULL},
    {0x3ff3430cd0f8939cULL, 0x3fcb391e5339b4fbULL, 0xbfba3fa5bd74b034ULL, 0x3faafbbcc41090cfULL, 0x402c14fb892c9302ULL, 0x3fd660c9c9e57ffcULL, 0x3fe4a3be805fb9d8ULL, 0x40fe24c000000000ULL},
    {0x3ff347104ae8bb11ULL, 0x3fcb4c6a32fae9feULL, 0xbfba32d4d564179aULL, 0x3faaee91f8b5defdULL, 0x402c2c638dfe564fULL, 0x3fd662ede1a44c63ULL, 0x3fe4a2b564c70d3fULL, 0x40fe24d000000000ULL},
    {0x3ff34b13c4d8e287ULL, 0x3fcb5fbbbacdf6c5ULL, 0xbfba25fc3324f6eeULL, 0x3faae15f3cfa462fULL, 0x402c43d230bcfba9ULL, 0x3fd66511f82ebd59ULL, 0x3fe4a1ac46107eeeULL, 0x40fe24e000000000ULL},
    {0x3ff34f173ec909fbULL, 0x3fcb7312eab2db56ULL, 0xbfba191bd6b74e2eULL, 0x3faad42490ddc667ULL, 0x402c5b4770f3c40dULL, 0x3fd667360d859329ULL, 0x3fe4a0a3243e0072ULL, 0x40fe24f000000000ULL},
    {0x3ff3531ab8b9316fULL, 0x3fcb866fc2a997a9ULL, 0xbfba0c33c01b1d5dULL, 0x3faac6e1f4605fa5ULL, 0x402c72c34e2df07bULL, 0x3fd6695a21a98d7bULL, 0x3fe49f99ff5181b8ULL, 0x40fe250000000000ULL},
    {0x3ff3571e32a958e4ULL, 0x3fcb99d242b22bc4ULL, 0xbfb9ff43ef50647aULL, 0x3faab997678211e9ULL, 0x402c8a45c7f6c1eeULL, 0x3fd66b7e349b6b59ULL, 0x3fe49e90d74cf115ULL, 0x40fe251000000000ULL},
};
static constexpr FrozenRklCell kScaledRkl2[] = {
    {0x3ff3b72541e9f389ULL, 0x3fc9266569fd5df0ULL, 0xbfb791ac0da668ebULL, 0x3fa77d0d9ffc6fe7ULL, 0x402b645fe7cc0daaULL, 0x3fd60c41afbd44ecULL, 0x3fe474a3d2caaf3dULL, 0x4123f7e600000000ULL},
    {0x3ff3bb3b37e10a3bULL, 0x3fc939387801d87eULL, 0xbfb7853e5cbbba18ULL, 0x3fa77032386ac3e0ULL, 0x402b7bb0ce57c3a6ULL, 0x3fd60e6b2c5a8333ULL, 0x3fe473aa298a0a11ULL, 0x4123f7e800000000ULL},
    {0x3ff3bf512dd820efULL, 0x3fc94c111b0a7575ULL, 0xbfb778c8ff2f1aedULL, 0x3fa7634ee07830e1ULL, 0x402b930863bdedc3ULL, 0x3fd61094a57ef900ULL, 0x3fe472b076d0d83dULL, 0x4123f7ea00000000ULL},
    {0x3ff3c36723cf37a2ULL, 0x3fc95eef531734d3ULL, 0xbfb76c4bf5008b69ULL, 0x3fa756639824b6e5ULL, 0x402baa66a788f354ULL, 0x3fd612be1b2ccd8cULL, 0x3fe471b6baa4f977ULL, 0x4123f7ec00000000ULL},
    {0x3ff3c77d19c64e55ULL, 0x3fc971d32028169dULL, 0xbfb75fc73e300b89ULL, 0x3fa749705f7055ebULL, 0x402bc1cb99433bbeULL, 0x3fd614e78d66264aULL, 0x3fe470bcf50c48a0ULL, 0x4123f7ee00000000ULL},
    {0x3ff3cb930fbd6506ULL, 0x3fc984bc823d1acbULL, 0xbfb7533adabd9b53ULL, 0x3fa73c75365b0dfbULL, 0x402bd93738772e5fULL, 0x3fd61710fc2d26ecULL, 0x3fe46fc3260c9bc3ULL, 0x4123f7f000000000ULL},
    {0x3ff3cfa905b47bb9ULL, 0x3fc997ab79564162ULL, 0xbfb746a6caa93ac4ULL, 0x3fa72f721ce4df0eULL, 0x402bf0a984af3292ULL, 0x3fd6193a6783f154ULL, 0x3fe46ec94dabc417ULL, 0x4123f7f200000000ULL},
    {0x3ff3d3befbab926bULL, 0x3fc9aaa005738a5fULL, 0xbfb73a0b0df2e9dcULL, 0x3fa72267130dc926ULL, 0x402c08227d75afb9ULL, 0x3fd61b63cf6ca5b2ULL, 0x3fe46dcf6bef8e0aULL, 0x4123f7f400000000ULL},
    {0x3ff3d7d4f1a2a920ULL, 0x3fc9bd9a2694f5c9ULL, 0xbfb72d67a49aa89dULL, 0x3fa7155418d5cc45ULL, 0x402c1fa222550d31ULL, 0x3fd61d8d33e96267ULL, 0x3fe46cd580ddc144ULL, 0x4123f7f600000000ULL},
    {0x3ff3dbeae799bfd2ULL, 0x3fc9d099dcba839bULL, 0xbfb720bc8ea07702ULL, 0x3fa708392e3ce868ULL, 0x402c372872d7b259ULL, 0x3fd61fb694fc4425ULL, 0x3fe46bdb8c7c20acULL, 0x4123f7f800000000ULL},
    {0x3ff3e000dd90d684ULL, 0x3fc9e39f27e433d3ULL, 0xbfb71409cc04550fULL, 0x3fa6fb1653431d91ULL, 0x402c4eb56e88068dULL, 0x3fd621dff2a765d1ULL, 0x3fe46ae18ed06a64ULL, 0x4123f7fa00000000ULL},
    {0x3ff3e416d387ed37ULL, 0x3fc9f6aa08120674ULL, 0xbfb7074f5cc642c4ULL, 0x3fa6edeb87e86bc0ULL, 0x402c664914f0712cULL, 0x3fd624094cece0a0ULL, 0x3fe469e787e057e2ULL, 0x4123f7fc00000000ULL},
    {0x3ff3e82cc97f03ebULL, 0x3fca09ba7d43fb7cULL, 0xbfb6fa8d40e6401fULL, 0x3fa6e0b8cc2cd2f1ULL, 0x402c7de3659b5997ULL, 0x3fd62632a3cecc0cULL, 0x3fe468ed77b19de2ULL, 0x4123f7fe00000000ULL},
    {0x3ff3ec42bf761a9dULL, 0x3fca1cd0877a12f0ULL, 0xbfb6edc378644d22ULL, 0x3fa6d37e20105328ULL, 0x402c95846013272bULL, 0x3fd6285bf74f3dd9ULL, 0x3fe467f35e49ec77ULL, 0x4123f80000000000ULL},
    {0x3ff3f058b56d3150ULL, 0x3fca2fec26b44cc7ULL, 0xbfb6e0f2034069cdULL, 0x3fa6c63b8392ec67ULL, 0x402cad2c03e24149ULL, 0x3fd62a8547704a0fULL, 0x3fe466f93baeef01ULL, 0x4123f80200000000ULL},
    {0x3ff3f46eab644802ULL, 0x3fca430d5af2a90aULL, 0xbfb6d418e17a9620ULL, 0x3fa6b8f0f6b49eaaULL, 0x402cc4da50930f4aULL, 0x3fd62cae94340306ULL, 0x3fe465ff0fe64c45ULL, 0x4123f80400000000ULL},
};
static constexpr FrozenRklCell kUnscaledRkl1[] = {
    {0x3ff302621189b1e3ULL, 0x3fcacd7974354e63ULL, 0xbfbb2e497c5e7f81ULL, 0x3fabedd7bf0cf87fULL, 0x402b278622b6c658ULL, 0x3fd6636db87a5c89ULL, 0x3fe4c410b174f3c6ULL, 0x40fe242000000000ULL},
    {0x3ff3066574828417ULL, 0x3fcae08cc34415abULL, 0xbfbb21c5da1f37a3ULL, 0x3fabe0fc577b4c77ULL, 0x402b3eabe332abbfULL, 0x3fd6658fa752b4e8ULL, 0x3fe4c301d66c2a96ULL, 0x40fe243000000000ULL},
    {0x3ff30a68d77b564bULL, 0x3fcaf3a5ba64b4b9ULL, 0xbfbb153a7db167b3ULL, 0x3fabd418ff88b976ULL, 0x402b55d8462ae94aULL, 0x3fd667b195e0d6faULL, 0x3fe4c1f2faa4b08cULL, 0x40fe244000000000ULL},
    {0x3ff30e6c3a742882ULL, 0x3fcb06c459972b8eULL, 0xbfbb08a767150fb1ULL, 0x3fabc72db7353f7bULL, 0x402b6d0b4b2abff0ULL, 0x3fd669d38424f19fULL, 0x3fe4c0e41e1efe23ULL, 0x40fe245000000000ULL},
    {0x3ff3126f9d6cfab6ULL, 0x3fcb19e8a0db7a27ULL, 0xbfbafc0c964a2f9bULL, 0x3fabba3a7e80de83ULL, 0x402b8444f1bd70b2ULL, 0x3fd66bf5721f3398ULL, 0x3fe4bfd540db8b70ULL, 0x40fe246000000000ULL},
    {0x3ff316730065cceaULL, 0x3fcb2d129031a088ULL, 0xbfbaef6a0b50c771ULL, 0x3fabad3f556b9691ULL, 0x402b9b85396e3c8eULL, 0x3fd66e175fcfcb75ULL, 0x3fe4bec662dad023ULL, 0x40fe247000000000ULL},
    {0x3ff31a76635e9f1dULL, 0x3fcb404227999eadULL, 0xbfbae2bfc628d735ULL, 0x3faba03c3bf567a3ULL, 0x402bb2cc21c86482ULL, 0x3fd670394d36e7a4ULL, 0x3fe4bdb7841d4387ULL, 0x40fe248000000000ULL},
    {0x3ff31e79c6577152ULL, 0x3fcb537767137497ULL, 0xbfbad60dc6d25ee7ULL, 0x3fab9331321e51bdULL, 0x402bca19aa57298cULL, 0x3fd6725b3a54b66bULL, 0x3fe4bca8a4a35c83ULL, 0x40fe249000000000ULL},
    {0x3ff3227d29504388ULL, 0x3fcb66b24e9f2249ULL, 0xbfbac9540d4d5e88ULL, 0x3fab861e37e654ddULL, 0x402be16dd2a5cca9ULL, 0x3fd6747d272965ebULL, 0x3fe4bb99c46d919aULL, 0x40fe24a000000000ULL},
    {0x3ff326808c4915bbULL, 0x3fcb79f2de3ca7c2ULL, 0xbfbabc929999d613ULL, 0x3fab79034d4d70ffULL, 0x402bf8c89a3f8ed9ULL, 0x3fd6769f13b5241bULL, 0x3fe4ba8ae37c58edULL, 0x40fe24b000000000ULL},
    {0x3ff32a83ef41e7efULL, 0x3fcb8d3915ec04fcULL, 0xbfbaafc96bb7c58bULL, 0x3fab6be07253a626ULL, 0x402c102a00afb117ULL, 0x3fd678c0fff81ecdULL, 0x3fe4b97c01d02837ULL, 0x40fe24c000000000ULL},
    {0x3ff32e87523aba23ULL, 0x3fcba084f5ad3a00ULL, 0xbfbaa2f883a72cf1ULL, 0x3fab5eb5a6f8f454ULL, 0x402c279205817464ULL, 0x3fd67ae2ebf283afULL, 0x3fe4b86d1f6974d5ULL, 0x40fe24d000000000ULL},
    {0x3ff3328ab5338c5aULL, 0x3fcbb3d67d8046c7ULL, 0xbfba961fe1680c45ULL, 0x3fab5182eb3d5b86ULL, 0x402c3f00a84019bdULL, 0x3fd67d04d7a48041ULL, 0x3fe4b75e3c48b3bcULL, 0x40fe24e000000000ULL},
    {0x3ff3368e182c5e8dULL, 0x3fcbc72dad652b57ULL, 0xbfba893f84fa6386ULL, 0x3fab44483f20dbbfULL, 0x402c5675e876e222ULL, 0x3fd67f26c30e41ebULL, 0x3fe4b64f586e5987ULL, 0x40fe24f000000000ULL},
    {0x3ff33a917b2530c2ULL, 0x3fcbda8a855be7abULL, 0xbfba7c576e5e32b4ULL, 0x3fab3705a2a374fcULL, 0x402c6df1c5b10e91ULL, 0x3fd68148ae2ff5dfULL, 0x3fe4b54073dada68ULL, 0x40fe250000000000ULL},
    {0x3ff33e94de1e02f7ULL, 0x3fcbeded05647bc7ULL, 0xbfba6f679d9379d1ULL, 0x3fab29bb15c52740ULL, 0x402c85743f79e004ULL, 0x3fd6836a9909c936ULL, 0x3fe4b4318e8eaa36ULL, 0x40fe251000000000ULL},
};
static constexpr FrozenRklCell kUnscaledRkl2[] = {
    {0x3ff368ebe3534d60ULL, 0x3fca39bd05dd825aULL, 0xbfba4e8b0ec9ae54ULL, 0x3fab022c584e5b6eULL, 0x402b5b610b117533ULL, 0x3fd6549af352fc57ULL, 0x3fe4b692577d770fULL, 0x40fe242000000000ULL},
    {0x3ff36d019bc62d6fULL, 0x3fca4c9013e1fce8ULL, 0xbfba421d5ddeff7fULL, 0x3faaf550f0bcaf66ULL, 0x402b72b1f19d2b30ULL, 0x3fd656be33ef104aULL, 0x3fe4b5872475e13aULL, 0x40fe243000000000ULL},
    {0x3ff3711754390d81ULL, 0x3fca5f68b6ea99dcULL, 0xbfba35a800526057ULL, 0x3faae86d98ca1c66ULL, 0x402b8a098703554aULL, 0x3fd658e173b0bbebULL, 0x3fe4b47bef2821a2ULL, 0x40fe244000000000ULL},
    {0x3ff3752d0cabed92ULL, 0x3fca7246eef7593cULL, 0xbfba292af623d0d0ULL, 0x3faadb825076a268ULL, 0x402ba167cace5addULL, 0x3fd65b04b29888c4ULL, 0x3fe4b370b795a6ebULL, 0x40fe245000000000ULL},
    {0x3ff37942c51ecda0ULL, 0x3fca852abc083b04ULL, 0xbfba1ca63f5350f2ULL, 0x3faace8f17c24172ULL, 0x402bb8ccbc88a348ULL, 0x3fd65d27f0a6fff6ULL, 0x3fe4b2657dbfde8cULL, 0x40fe246000000000ULL},
    {0x3ff37d587d91adb1ULL, 0x3fca98141e1d3f34ULL, 0xbfba1019dbe0e0bbULL, 0x3faac193eeacf981ULL, 0x402bd0385bbc95e8ULL, 0x3fd65f4b2ddcaa21ULL, 0x3fe4b15a41a834bfULL, 0x40fe247000000000ULL},
    {0x3ff3816e36048dc1ULL, 0x3fcaab03153665cbULL, 0xbfba0385cbcc802cULL, 0x3faab490d536ca92ULL, 0x402be7aaa7f49a1bULL, 0x3fd6616e6a3a0f7bULL, 0x3fe4b04f03501498ULL, 0x40fe248000000000ULL},
    {0x3ff38583ee776dd0ULL, 0x3fcabdf7a153aecaULL, 0xbfb9f6ea0f162f44ULL, 0x3faaa785cb5fb4acULL, 0x402bff23a0bb1744ULL, 0x3fd66391a5bfb7cfULL, 0x3fe4af43c2b8e7f4ULL, 0x40fe249000000000ULL},
    {0x3ff38999a6ea4de2ULL, 0x3fcad0f1c2751a31ULL, 0xbfb9ea46a5bdee04ULL, 0x3faa9a72d127b7caULL, 0x402c16a3459a74baULL, 0x3fd665b4e06e2a63ULL, 0x3fe4ae387fe41782ULL, 0x40fe24a000000000ULL},
    {0x3ff38daf5f5d2df3ULL, 0x3fcae3f1789aa804ULL, 0xbfb9dd9b8fc3bc69ULL, 0x3faa8d57e68ed3eeULL, 0x402c2e29961d19e2ULL, 0x3fd667d81a45ee1eULL, 0x3fe4ad2d3ad30ac8ULL, 0x40fe24b000000000ULL},
    {0x3ff391c517d00e02ULL, 0x3fcaf6f6c3c4583bULL, 0xbfb9d0e8cd279a77ULL, 0x3faa80350b950916ULL, 0x402c45b691cd6e16ULL, 0x3fd669fb5347896fULL, 0x3fe4ac21f3872818ULL, 0x40fe24c000000000ULL},
    {0x3ff395dad042ee10ULL, 0x3fcb0a01a3f22adcULL, 0xbfb9c42e5de9882cULL, 0x3faa730a403a5744ULL, 0x402c5d4a3835d8b6ULL, 0x3fd66c1e8b738257ULL, 0x3fe4ab16aa01d4a4ULL, 0x40fe24d000000000ULL},
    {0x3ff399f088b5ce21ULL, 0x3fcb1d1219241fe5ULL, 0xbfb9b76c42098585ULL, 0x3faa65d7847ebe74ULL, 0x402c74e488e0c121ULL, 0x3fd66e41c2ca5e60ULL, 0x3fe4aa0b5e447469ULL, 0x40fe24e000000000ULL},
    {0x3ff39e064128ae32ULL, 0x3fcb3028235a3757ULL, 0xbfb9aaa27987928bULL, 0x3faa589cd8623eaeULL, 0x402c8c8583588eb6ULL, 0x3fd67064f94ca2b0ULL, 0x3fe4a90010506a43ULL, 0x40fe24f000000000ULL},
    {0x3ff3a21bf99b8e42ULL, 0x3fcb4343c2947131ULL, 0xbfb99dd10463af36ULL, 0x3faa4b5a3be4d7ecULL, 0x402ca42d2727a8d1ULL, 0x3fd672882efad3f8ULL, 0x3fe4a7f4c02717e3ULL, 0x40fe250000000000ULL},
    {0x3ff3a631b20e6e52ULL, 0x3fcb5664f6d2cd74ULL, 0xbfb990f7e29ddb87ULL, 0x3faa3e0faf068a2fULL, 0x402cbbdb73d876d2ULL, 0x3fd674ab63d57677ULL, 0x3fe4a6e96dc9ddd7ULL, 0x40fe251000000000ULL},
};


void fail(const std::string& message)
{
    std::cerr << message << '\n';
    ++failures;
}

void require_cuda(cudaError_t error, const char* operation)
{
    if (error != cudaSuccess) {
        fail(std::string(operation) + ": " + cudaGetErrorString(error));
    }
}

void expect_bits(const char* label, double actual, std::uint64_t expected)
{
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(actual);
    if (bits != expected) {
        std::ostringstream message;
        message << label << " raw authority mismatch: actual=0x"
                << std::hex << bits << " expected=0x" << expected;
        fail(message.str());
    }
}

void expect_close(const char* label, double actual, double expected,
                  double relative_limit = 5.0e-12)
{
    if (std::isnan(expected)) {
        if (!std::isnan(actual)) fail(std::string(label) + " expected NaN");
        return;
    }
    if (!std::isfinite(expected) || expected == 0.0) {
        if (std::bit_cast<std::uint64_t>(actual)
            != std::bit_cast<std::uint64_t>(expected)) {
            fail(std::string(label) + " exact branch/value mismatch");
        }
        return;
    }
    if (!std::isfinite(actual)) {
        fail(std::string(label) + " non-finite device value");
        return;
    }
    const double relative = std::abs(actual - expected) / std::abs(expected);
    if (relative > relative_limit) {
        std::ostringstream message;
        message << std::setprecision(17) << label
                << " actual=" << actual << " expected=" << expected
                << " relative_error=" << relative
                << " limit=" << relative_limit;
        fail(message.str());
    }
}

bool budget_comparison_accepts(double actual, double expected, int cell,
                               const NumericBudget& budget,
                               ErrorEnvelope& envelope)
{
    if (std::bit_cast<std::uint64_t>(actual)
        == std::bit_cast<std::uint64_t>(expected))
        return true;
    constexpr std::uint64_t exponent_mask = 0x7ff0000000000000ULL;
    const bool actual_is_finite =
        (std::bit_cast<std::uint64_t>(actual) & exponent_mask)
        != exponent_mask;
    const bool expected_is_finite =
        (std::bit_cast<std::uint64_t>(expected) & exponent_mask)
        != exponent_mask;
    if (!actual_is_finite || !expected_is_finite) return false;
    const double absolute = std::abs(actual - expected);
    const double relative = expected == 0.0
        ? std::numeric_limits<double>::infinity()
        : absolute / std::abs(expected);
    if (absolute > envelope.maximum_absolute) {
        envelope.maximum_absolute = absolute;
        envelope.absolute_cell = cell;
    }
    if (relative > envelope.maximum_relative) {
        envelope.maximum_relative = relative;
        envelope.relative_cell = cell;
    }
    return !(budget.exact || expected == 0.0
             || (absolute > budget.absolute
                 && relative > budget.relative));
}

void compare_with_budget(const char* label, double actual, double expected,
                         int cell, const NumericBudget& budget,
                         ErrorEnvelope& envelope)
{
    if (!budget_comparison_accepts(
            actual, expected, cell, budget, envelope)) {
        const double absolute = std::abs(actual - expected);
        const double relative = expected == 0.0
            ? std::numeric_limits<double>::infinity()
            : absolute / std::abs(expected);
        std::ostringstream message;
        message << std::setprecision(17) << label
                << " cell=" << cell << " actual=" << actual
                << " expected=" << expected
                << " absolute_error=" << absolute
                << " relative_error=" << relative
                << " absolute_limit=" << budget.absolute
                << " relative_limit=" << budget.relative;
        fail(message.str());
    }
}

void verify_budget_comparator_special_values()
{
    constexpr NumericBudget budget{1.0e-12, 1.0e-12, false};
    ErrorEnvelope envelope{};
    volatile std::uint64_t nan_bits = 0x7ff8000000000001ULL;
    volatile std::uint64_t positive_inf_bits = 0x7ff0000000000000ULL;
    volatile std::uint64_t negative_inf_bits = 0xfff0000000000000ULL;
    const double nan = std::bit_cast<double>(
        static_cast<std::uint64_t>(nan_bits));
    const double positive_inf = std::bit_cast<double>(
        static_cast<std::uint64_t>(positive_inf_bits));
    const double negative_inf = std::bit_cast<double>(
        static_cast<std::uint64_t>(negative_inf_bits));
    if (budget_comparison_accepts(nan, 1.0, 0, budget, envelope)
        || budget_comparison_accepts(1.0, nan, 0, budget, envelope)
        || budget_comparison_accepts(positive_inf, 1.0, 0, budget, envelope)
        || budget_comparison_accepts(1.0, positive_inf, 0, budget, envelope)
        || budget_comparison_accepts(negative_inf, 1.0, 0, budget, envelope)
        || budget_comparison_accepts(1.0, negative_inf, 0, budget, envelope)) {
        fail("budget comparator accepted mismatched non-finite value");
    }
    if (!budget_comparison_accepts(
            positive_inf, positive_inf, 0, budget, envelope)
        || !budget_comparison_accepts(
            negative_inf, negative_inf, 0, budget, envelope)
        || !budget_comparison_accepts(nan, nan, 0, budget, envelope)
        || !budget_comparison_accepts(0.0, 0.0, 0, budget, envelope)
        || !budget_comparison_accepts(-0.0, -0.0, 0, budget, envelope)
        || budget_comparison_accepts(0.0, -0.0, 0, budget, envelope)) {
        fail("budget comparator raw-exact/signed-zero semantics drifted");
    }
}

using RklBudgets = std::array<NumericBudget, 8>;

// Frozen values characterize the CPU formula, while CUDA instruction
// selection differs slightly across GPU ISAs.  Eight e-16 relative (and
// eight e-15 absolute near zero) defines this O(ULP) parity gate.
constexpr NumericBudget kRklPortableBudget{8.0e-15, 8.0e-16, false};
constexpr NumericBudget kRklExactBudget{0.0, 0.0, true};
constexpr RklBudgets kFirstRklBudgets = {
    kRklPortableBudget, kRklPortableBudget, kRklPortableBudget,
    kRklPortableBudget, kRklPortableBudget, kRklPortableBudget,
    kRklPortableBudget, kRklExactBudget};
constexpr RklBudgets kScaledRkl1Budgets = {
    kRklPortableBudget, kRklPortableBudget, kRklPortableBudget,
    kRklPortableBudget, kRklPortableBudget, kRklPortableBudget,
    kRklPortableBudget, kRklExactBudget};
constexpr RklBudgets kScaledRkl2Budgets = {
    kRklPortableBudget, kRklPortableBudget, kRklPortableBudget,
    kRklPortableBudget, kRklPortableBudget, kRklPortableBudget,
    kRklPortableBudget, kRklExactBudget};
constexpr RklBudgets kUnscaledRkl1Budgets = {
    kRklPortableBudget, kRklPortableBudget, kRklPortableBudget,
    kRklPortableBudget, kRklPortableBudget, kRklPortableBudget,
    kRklPortableBudget, kRklExactBudget};
constexpr RklBudgets kUnscaledRkl2Budgets = {
    kRklPortableBudget, kRklPortableBudget, kRklPortableBudget,
    kRklPortableBudget, kRklPortableBudget, kRklPortableBudget,
    kRklPortableBudget, kRklExactBudget};

template <std::size_t N>
void compare_frozen_rkl_state(
    const char* stage, const FluidState& actual,
    const FrozenRklCell (&expected)[N], const Grid& grid,
    const RklBudgets& budgets)
{
    static constexpr const char* kFields[8] = {
        "rho", "mom_u", "mom_v", "mom_w", "eng", "X0", "X1", "ENUC"};
    if (N != static_cast<std::size_t>(grid.Ie() - grid.Is())) {
        fail(std::string(stage) + " frozen active extent drifted");
        return;
    }
    std::array<ErrorEnvelope, 8> envelopes{};
    for (std::size_t active = 0; active < N; ++active) {
        const int cell = grid.GetIndex(
            grid.Is() + static_cast<int>(active), grid.Js(), grid.Ks());
        const double values[8] = {
            actual.rho[cell], actual.mom_u[cell], actual.mom_v[cell],
            actual.mom_w[cell], actual.eng[cell], actual.X(0, cell),
            actual.X(1, cell), actual.enuc_rate[cell]};
        const std::uint64_t bits[8] = {
            expected[active].rho, expected[active].mom_u,
            expected[active].mom_v, expected[active].mom_w,
            expected[active].eng, expected[active].species0,
            expected[active].species1, expected[active].enuc};
        for (int field = 0; field < 8; ++field) {
            const std::string label = std::string(stage) + "." + kFields[field];
            compare_with_budget(
                label.c_str(), values[field],
                std::bit_cast<double>(bits[field]), cell, budgets[field],
                envelopes[field]);
        }
    }
    for (int field = 0; field < 8; ++field) {
        std::cout << std::setprecision(17)
                  << "C4_RKL_ENVELOPE stage=" << stage
                  << " field=" << kFields[field]
                  << " max_abs=" << envelopes[field].maximum_absolute
                  << " abs_cell=" << envelopes[field].absolute_cell
                  << " max_rel=" << envelopes[field].maximum_relative
                  << " rel_cell=" << envelopes[field].relative_cell << '\n';
    }
}

Grid make_grid(int dimension, double cell_width = 0.01)
{
    Grid grid(2,
              0.0, amr::BLOCK_NX * cell_width,
              0.0, amr::BLOCK_NY * cell_width,
              0.0, amr::BLOCK_NZ * cell_width);
    grid.dim = dimension;
    grid.geometry = "cartesian";
    grid.InitializeTopology();
    return grid;
}

FluidState make_state(const Grid& grid, int species_count)
{
    FluidState state;
    state.Preallocate(grid.GetTotalSize());
    state.InitSpecies(species_count);
    for (int k = 0; k < grid.GetTotalZ(); ++k) {
        for (int j = 0; j < grid.GetTotalY(); ++j) {
            for (int i = 0; i < grid.GetTotalX(); ++i) {
                const int cell = grid.GetIndex(i, j, k);
                const double q = 0.013 * i + 0.021 * j + 0.034 * k;
                const double rho = 1.1 + 0.07 * q;
                const double u = 0.2 + 0.03 * q;
                const double v = -0.1 + 0.02 * q;
                const double w = 0.05 - 0.01 * q;
                const double x0 = species_count > 0 ? 0.35 + 0.01 * q : 0.0;
                const double x1 = species_count > 1 ? 1.0 - x0 : 0.0;
                const double cv = species_count > 1
                    ? x0 * 3.5 + x1 * 7.25 : 718.0;
                const double temperature = 2.0 + 0.4 * q;
                state.rho[cell] = rho;
                state.mom_u[cell] = rho * u;
                state.mom_v[cell] = rho * v;
                state.mom_w[cell] = rho * w;
                state.eng[cell] = rho * cv * temperature
                    + 0.5 * rho * (u * u + v * v + w * w);
                state.enuc_rate[cell] = 9000.0 + cell;
                if (species_count > 0) state.X(0, cell) = x0;
                if (species_count > 1) state.X(1, cell) = x1;
            }
        }
    }
    return state;
}

SimConfig make_config(bool thermal, bool viscous, bool species)
{
    SimConfig config{};
    config.physics.diffusion.use_diffusion = true;
    config.physics.diffusion.use_thermal_diffusion = thermal;
    config.physics.diffusion.use_viscous_diffusion = viscous;
    config.physics.diffusion.use_species_diffusion = species;
    config.physics.diffusion.alpha_therm = 0.37;
    config.physics.diffusion.nu_visc = 0.19;
    config.physics.diffusion.D_spec = 0.11;
    config.physics.diffusion.diff_cfl = 0.73;
    return config;
}

SpeciesManager make_species()
{
    SpeciesManager species;
    species.add_species("a", 1.0, 1.0, 1.4, 3.5);
    species.add_species("b", 4.0, 2.0, 1.5, 7.25);
    return species;
}

// Independent constant-transport, IdealGas mixture capacity reference. The
// fixture's species have cv=(3.5,7.25); no EOS, face helper or production dt is
// called here. Long-double face/cell ratios implement the scalar row diagonal.
double cartesian_face_dt_reference(const FluidState& state, const Grid& grid, const SimConfig& config)
{
    const auto& diffusion = config.physics.diffusion;
    long double maximum_rate = 0.;
    const auto capacity = [&](int cell) {
        return 3.5L*state.X(0,cell) + 7.25L*state.X(1,cell);
    };
    for (int k=grid.Ks(); k<grid.Ke(); ++k)
    for (int j=grid.Js(); j<grid.Je(); ++j)
    for (int i=grid.Is(); i<grid.Ie(); ++i) {
        const int cell = grid.GetIndex(i,j,k);
        const long double rho = state.rho[cell], cv = capacity(cell);
        long double rate = 0.;
        for (int direction=0; direction<grid.dim; ++direction) {
            const int stride = direction == 0 ? 1 : direction == 1 ? grid.stride_y : grid.stride_z;
            const long double h = direction == 0 ? grid.dx1 : direction == 1 ? grid.dx2 : grid.dx3;
            for (int sign : {-1, 1}) {
                const int adjacent = cell+sign*stride;
                const long double rho_ratio = (rho+state.rho[adjacent])/(2*rho);
                long double effective = 0.;
                if (diffusion.use_viscous_diffusion) effective = diffusion.nu_visc*rho_ratio;
                if (diffusion.use_species_diffusion)
                    effective = std::max(effective, diffusion.D_spec*rho_ratio);
                if (diffusion.use_thermal_diffusion)
                    effective = std::max(effective, diffusion.alpha_therm*rho_ratio
                        *(cv+capacity(adjacent))/(2*cv));
                rate += effective/(h*h);
            }
        }
        maximum_rate = std::max(maximum_rate, rate);
    }
    return maximum_rate > 0. ? static_cast<double>(std::min(1.e10L, 1/maximum_rate)) : 1.e10;
}

void verify_frozen_host_authority()
{
    const double composition[3] = {0.2, 0.3, 0.5};
    const double charge[3] = {1.0, 2.0, 8.0};
    const double inverse_mass[3] = {1.0, 0.25, 1.0 / 16.0};
    // The ordinary-g++ characterization freezes this value bitwise. NVCC's
    // host front-end differs by two ULPs under the target's device-exact flags.
    expect_close("conductivity.iec_times_zbar.nvcc_host",
        ConductivityMath::compute_stellar_conductivity(
            8.7e7, 3.4e6, 5.2e21, 7.3e29, 1.7,
            composition, 3, charge, inverse_mass),
        std::bit_cast<double>(0x4361a6e6e5986971ULL), 5.0e-15);

    SpeciesManager species = make_species();
    IdealGas eos(1.37, species);
    const std::uint64_t dt_bits[3] = {
        0x3f21b661f8cde833ULL, 0x3f11b661f8cde833ULL,
        0x3f079dd7f667e044ULL};
    for (int dimension = 1; dimension <= 3; ++dimension) {
        const Grid grid = make_grid(dimension);
        const FluidState state = make_state(grid, 2);
        const double spacing[]{grid.dx1, grid.dx2, grid.dx3};
        // The bitwise control describes uniform capacities. The separate
        // face-capacity reference checks the varying-capacity stability step.
        expect_bits("uniform_cartesian.raw_fe", DiffFlux::raw_forward_euler_candidate(.37, spacing, dimension),
                    dt_bits[dimension-1]);
        expect_close("cartesian.face_capacity_dt",
            DiffFlux::adaptive_dt_diff(
                state, eos, grid, make_config(true, true, true), 1.0),
            cartesian_face_dt_reference(state, grid, make_config(true, true, true)));
    }

    const Grid grid = make_grid(1);
    const FluidState state = make_state(grid, 2);
    const int face = grid.GetIndex(grid.Is() + 5, 0, 0);
    const int cell = grid.GetIndex(grid.Is() + 6, 0, 0);
    struct RouteAuthority {
        bool thermal, viscous, species;
        std::uint64_t flux_u, flux_v, flux_w, flux_e, flux_x0, flux_x1;
        std::uint64_t op_u, op_v, op_w, op_e, op_x0, op_x1;
    };
    const RouteAuthority routes[] = {
        {true, false, false,
         0, 0, 0, 0xbff43400198d8819ULL, 0, 0,
         0, 0, 0, 0x3fb7efc1166a8e00ULL, 0, 0},
        {false, true, false,
         0xbf80c8737bec97afULL, 0xbf766099fa90cb42ULL,
         0x3f666099fa90cb42ULL, 0xbf50318f4b8f7948ULL, 0, 0,
         0x3f461885b2dc5d00ULL, 0x3f3d760799250d80ULL,
         0xbf2d760799250d80ULL, 0x3f42fef32c7b3be0ULL, 0, 0},
        {false, false, true,
         0, 0, 0, 0, 0xbf59e91e14a7abdaULL, 0x3f59e91e14a7b804ULL,
         0, 0, 0, 0, 0x3f210e703064c200ULL, 0xbf210e70303eaf20ULL},
        {true, true, true,
         0xbf80c8737bec97afULL, 0xbf766099fa90cb42ULL,
         0x3f666099fa90cb42ULL, 0xbff4380c7d606bf8ULL,
         0xbf59e91e14a7abdaULL, 0x3f59e91e14a7b804ULL,
         0x3f461885b2dc5d00ULL, 0x3f3d760799250d80ULL,
         0xbf2d760799250d80ULL, 0x3fb815befcc38640ULL,
         0x3f210e703064c200ULL, 0xbf210e70303eaf20ULL}};
    for (const RouteAuthority& route : routes) {
        const SimConfig config = make_config(
            route.thermal, route.viscous, route.species);
        std::vector<FluidVector> flux(grid.GetTotalSize());
        std::vector<double> species_flux(2 * grid.GetTotalSize(), 0.0);
        DiffFlux::compute_fluxes(
            state, eos, grid, config, flux, species_flux, 0);
        expect_close("route.flux.u", flux[face].mom_u,
                     std::bit_cast<double>(route.flux_u), 1.0e-9);
        expect_close("route.flux.v", flux[face].mom_v,
                     std::bit_cast<double>(route.flux_v), 1.0e-9);
        expect_close("route.flux.w", flux[face].mom_w,
                     std::bit_cast<double>(route.flux_w), 1.0e-9);
        expect_close("route.flux.e", flux[face].eng,
                     std::bit_cast<double>(route.flux_e), 1.0e-9);
        expect_close("route.flux.x0", species_flux[face],
                     std::bit_cast<double>(route.flux_x0), 1.0e-9);
        expect_close("route.flux.x1",
                     species_flux[grid.GetTotalSize() + face],
                     std::bit_cast<double>(route.flux_x1), 1.0e-9);

        FluidState op;
        op.Preallocate(grid.GetTotalSize());
        op.InitSpecies(2);
        std::fill(op.enuc_rate.begin(), op.enuc_rate.end(), -777.0);
        DiffFlux::compute_diffusion_operator(state, op, eos, grid, config);
        expect_close("route.op.u", op.mom_u[cell],
                     std::bit_cast<double>(route.op_u), 1.0e-9);
        expect_close("route.op.v", op.mom_v[cell],
                     std::bit_cast<double>(route.op_v), 1.0e-9);
        expect_close("route.op.w", op.mom_w[cell],
                     std::bit_cast<double>(route.op_w), 1.0e-9);
        expect_close("route.op.e", op.eng[cell],
                     std::bit_cast<double>(route.op_e), 1.0e-9);
        expect_close("route.op.x0", op.X(0, cell),
                     std::bit_cast<double>(route.op_x0), 1.0e-9);
        expect_close("route.op.x1", op.X(1, cell),
                     std::bit_cast<double>(route.op_x1), 1.0e-9);
        expect_bits("route.op.enuc", op.enuc_rate[cell], 0xc088480000000000ULL);
    }

    SimConfig disabled = make_config(true, true, true);
    disabled.physics.diffusion.use_diffusion = false;
    expect_bits("master_off.sentinel",
        DiffFlux::adaptive_dt_diff(state, eos, grid, disabled, 0.13),
        0x4202a05f20000000ULL);
    expect_close("raw_fe.multiplier_one",
        DiffFlux::adaptive_dt_diff(
            state, eos, grid, make_config(true, true, true), 1.0),
        cartesian_face_dt_reference(state, grid, make_config(true, true, true)));
    expect_close("driver_cfl.applied_once",
        DiffFlux::adaptive_dt_diff(
            state, eos, grid, make_config(true, true, true), 0.73),
        .73*cartesian_face_dt_reference(state, grid, make_config(true, true, true)));

    const auto below = std::nextafter(1.0e-12, 0.0);
    const auto above = std::nextafter(
        1.0e-12, std::numeric_limits<double>::infinity());
    SimConfig coefficient = make_config(true, false, false);
    for (double positive : {below, 1.e-12, above}) {
        coefficient.physics.diffusion.alpha_therm = positive;
        expect_close("positive_transport_near_old_cutoff",
            DiffFlux::adaptive_dt_diff(state, eos, grid, coefficient, 1.0),
            cartesian_face_dt_reference(state, grid, coefficient));
    }
    coefficient.physics.diffusion.alpha_therm = 0.;
    expect_bits("zero_transport.sentinel", DiffFlux::adaptive_dt_diff(state, eos, grid, coefficient, 1.),
                0x4202a05f20000000ULL);
}

template <class T>
struct DeviceArray
{
    T* pointer = nullptr;
    std::size_t count = 0;

    DeviceArray() = default;
    explicit DeviceArray(std::size_t size) : count(size)
    {
        if (count > 0) require_cuda(
            cudaMalloc(reinterpret_cast<void**>(&pointer), count * sizeof(T)),
            "cudaMalloc");
    }
    DeviceArray(const DeviceArray&) = delete;
    DeviceArray& operator=(const DeviceArray&) = delete;
    DeviceArray(DeviceArray&& other) noexcept
        : pointer(std::exchange(other.pointer, nullptr)), count(other.count) {}
    ~DeviceArray() { if (pointer) cudaFree(pointer); }

    void upload(const T* source, std::size_t size)
    {
        require_cuda(cudaMemcpy(pointer, source, size * sizeof(T),
                                cudaMemcpyHostToDevice), "upload");
    }
    void download(T* destination, std::size_t size) const
    {
        require_cuda(cudaMemcpy(destination, pointer, size * sizeof(T),
                                cudaMemcpyDeviceToHost), "download");
    }
};

struct DeviceStateOwner
{
    DeviceArray<double> rho, mom_u, mom_v, mom_w, eng, enuc, species;
    arch::cuda::DeviceStateView view{};

    DeviceStateOwner(int total, int n_species)
        : rho(total), mom_u(total), mom_v(total), mom_w(total), eng(total),
          enuc(total), species(static_cast<std::size_t>(total) * n_species)
    {
        view = {rho.pointer, mom_u.pointer, mom_v.pointer, mom_w.pointer,
                eng.pointer, enuc.pointer, species.pointer, total, n_species};
    }

    void upload(const FluidState& state)
    {
        const std::size_t total = state.rho.size();
        rho.upload(state.rho.data(), total);
        mom_u.upload(state.mom_u.data(), total);
        mom_v.upload(state.mom_v.data(), total);
        mom_w.upload(state.mom_w.data(), total);
        eng.upload(state.eng.data(), total);
        enuc.upload(state.enuc_rate.data(), total);
        if (!state.mass_fractions.empty())
            species.upload(state.mass_fractions.data(), state.mass_fractions.size());
    }

    FluidState download() const
    {
        FluidState state;
        state.Preallocate(view.total_size);
        state.InitSpecies(view.n_species);
        rho.download(state.rho.data(), state.rho.size());
        mom_u.download(state.mom_u.data(), state.mom_u.size());
        mom_v.download(state.mom_v.data(), state.mom_v.size());
        mom_w.download(state.mom_w.data(), state.mom_w.size());
        eng.download(state.eng.data(), state.eng.size());
        enuc.download(state.enuc_rate.data(), state.enuc_rate.size());
        if (!state.mass_fractions.empty())
            species.download(state.mass_fractions.data(), state.mass_fractions.size());
        return state;
    }
};

struct DeviceFixture
{
    Grid grid = make_grid(1);
    FluidState host_state = make_state(grid, 2);
    DeviceStateOwner state{grid.GetTotalSize(), 2};
    DeviceStateOwner output{grid.GetTotalSize(), 2};
    DeviceStateOwner flux{grid.GetTotalSize(), 2};
    DeviceArray<double> candidates{static_cast<std::size_t>(
        (grid.Ie() - grid.Is()) * (grid.Je() - grid.Js()) *
        (grid.Ke() - grid.Ks()))};
    DeviceArray<double> result{1};
    DeviceArray<int> status{1};
    DeviceArray<double> volume{static_cast<std::size_t>(grid.GetTotalSize())};
    DeviceArray<double> lower{static_cast<std::size_t>(grid.GetTotalSize())};
    DeviceArray<double> upper{static_cast<std::size_t>(grid.GetTotalSize())};
    DeviceArray<double> A{2}, Z{2}, gamma{2}, cv{2};
    IdealGasView eos{};
    arch::cuda::DeviceGridView device_grid{};
    arch::cuda::DiffusionWorkspaceView workspace{};

    DeviceFixture()
    {
        state.upload(host_state);
        FluidState sentinel = make_state(grid, 2);
        std::fill(sentinel.rho.begin(), sentinel.rho.end(), -101.0);
        std::fill(sentinel.mom_u.begin(), sentinel.mom_u.end(), -102.0);
        std::fill(sentinel.mom_v.begin(), sentinel.mom_v.end(), -103.0);
        std::fill(sentinel.mom_w.begin(), sentinel.mom_w.end(), -104.0);
        std::fill(sentinel.eng.begin(), sentinel.eng.end(), -105.0);
        std::fill(sentinel.enuc_rate.begin(), sentinel.enuc_rate.end(), -777.0);
        std::fill(sentinel.mass_fractions.begin(), sentinel.mass_fractions.end(), -106.0);
        output.upload(sentinel);
        flux.upload(sentinel);

        std::vector<double> metric(grid.GetTotalSize(), grid.dx1);
        std::vector<double> area(grid.GetTotalSize(), 1.0);
        volume.upload(metric.data(), metric.size());
        lower.upload(area.data(), area.size());
        upper.upload(area.data(), area.size());
        device_grid = arch::cuda::make_device_grid_view(grid);
        device_grid.cell_volume = volume.pointer;
        device_grid.face_area_lower[0] = lower.pointer;
        device_grid.face_area_upper[0] = upper.pointer;

        const double hA[2] = {1.0, 4.0};
        const double hZ[2] = {1.0, 2.0};
        const double hgamma[2] = {1.4, 1.5};
        const double hcv[2] = {3.5, 7.25};
        A.upload(hA, 2); Z.upload(hZ, 2); gamma.upload(hgamma, 2); cv.upload(hcv, 2);
        eos.species = {A.pointer, Z.pointer, gamma.pointer, cv.pointer, 2};
        eos.global_gamma = 1.37;
        workspace = {flux.view, candidates.pointer, result.pointer, status.pointer};
    }
};

__global__ void conductivity_probe(double* result)
{
    const double composition[3] = {0.2, 0.3, 0.5};
    const double charge[3] = {1.0, 2.0, 8.0};
    const double inverse_mass[3] = {1.0, 0.25, 1.0 / 16.0};
    *result = ConductivityMath::compute_stellar_conductivity(
        8.7e7, 3.4e6, 5.2e21, 7.3e29, 1.7,
        composition, 3, charge, inverse_mass);
}

__global__ void inverse_dt_floor_probe(double* result)
{
    const double spacing[1] = {2.0e4};
    *result = DiffFlux::raw_forward_euler_candidate(2.0e-12, spacing, 1);
}

__global__ void threshold_probe(double* result)
{
    const double floor = 1.0e-12;
    const double below = nextafter(floor, 0.0);
    const double above = nextafter(floor, 1.0);
    result[0] = DiffFlux::diffusion_face_is_active(below, 1.0) ? 1.0 : 0.0;
    result[1] = DiffFlux::diffusion_face_is_active(floor, 1.0) ? 1.0 : 0.0;
    result[2] = DiffFlux::diffusion_face_is_active(above, 1.0) ? 1.0 : 0.0;

    const double unit_spacing[1] = {0.01};
    result[3] = DiffFlux::raw_forward_euler_candidate(
        below, unit_spacing, 1);
    result[4] = DiffFlux::raw_forward_euler_candidate(
        floor, unit_spacing, 1);
    result[5] = DiffFlux::raw_forward_euler_candidate(
        above, unit_spacing, 1);

    const double exact_spacing[1] = {2.0e4};
    const double lower_inverse_spacing[1] = {
        nextafter(2.0e4, 3.0e4)};
    const double upper_inverse_spacing[1] = {nextafter(2.0e4, 0.0)};
    result[6] = DiffFlux::raw_forward_euler_candidate(
        2.0e-12, lower_inverse_spacing, 1);
    result[7] = DiffFlux::raw_forward_euler_candidate(
        2.0e-12, exact_spacing, 1);
    result[8] = DiffFlux::raw_forward_euler_candidate(
        2.0e-12, upper_inverse_spacing, 1);

    DiffFlux::DiffusionConfigView config{};
    config.use_diffusion = true;
    config.use_thermal_diffusion = true;
    DiffFlux::DiffusionCoefficients coefficients{};
    coefficients.alpha_therm = 1.0;
    FluidVector flux{};
    const FluidVector state{1.0, 0.0, 0.0, 0.0, 1.0};
    DiffFlux::assemble_diffusion_face_flux(
        state, state, 0.0, 1.0, 1.0, nullptr, nullptr, 0, 1.0,
        below, coefficients, config, flux, nullptr, 0);
    result[9] = flux.eng;
    DiffFlux::assemble_diffusion_face_flux(
        state, state, 0.0, 1.0, 1.0, nullptr, nullptr, 0, 1.0,
        floor, coefficients, config, flux, nullptr, 0);
    result[10] = flux.eng;
    DiffFlux::assemble_diffusion_face_flux(
        state, state, 0.0, 1.0, 1.0, nullptr, nullptr, 0, 1.0,
        above, coefficients, config, flux, nullptr, 0);
    result[11] = flux.eng;
}

struct StellarOverrideProbeEos
{
    ARCH_INLINE double get_temperature(
        double, double internal_energy, const double*) const
    {
        return internal_energy;
    }

    ARCH_INLINE double get_cv(double, double, const double*) const
    {
        return 1.0;
    }

    ARCH_INLINE void evaluate_state(eos_state_t& state) const
    {
        state.xne = 1.0;
        state.pele = 1.0;
        state.eta = 0.0;
        state.cv = 1.0;
    }
};

template<class Eos>
void verify_tabular_diffusion_latch(Eos eos, int compositions)
{
    DeviceFixture fixture;
    for (int cell = 0; cell < fixture.grid.GetTotalSize(); ++cell) {
        fixture.host_state.rho[cell] = 1.0;
        fixture.host_state.mom_u[cell] = fixture.host_state.mom_v[cell]
            = fixture.host_state.mom_w[cell] = 0.0;
        fixture.host_state.eng[cell] = 3.0;
    }
    fixture.state.upload(fixture.host_state);
    const int extent = 4 * compositions;
    const double jets[]{3.0, 2.0, 0.0, 0.0, 2.0, -3.0, 0.0, 0.0, 0.0};
    std::vector<double> table(tabular_eos::FieldCount * extent);
    DeviceArray<double> device_table(table.size());
    eos.uses_free_energy = true;
    const auto config = DiffFlux::make_diffusion_config_view(make_config(true, false, false));
    double previous_dt = 0.0;
    for (const bool invalid : {true, false, false}) {
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            std::fill_n(table.data() + field * extent, extent, jets[field]);
        // The boundary energy query fails, but Newton's upper-knot query is
        // valid and returns finite T=10. Constant coefficients then yield a
        // finite dt and zero thermal flux. Final-output checks cannot detect
        // this failure; only the shared Tabular boundary hook can report it.
        if (invalid)
            for (int rho = 0; rho < 2; ++rho)
                std::fill_n(table.data() + tabular_eos::Fyy * extent
                    + rho * 2 * compositions, compositions, 0.0);
        Eos host = eos;
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            host.free_energy_fields[field] = table.data() + field * extent;
        bool host_threw = false;
        try { static_cast<void>(host.get_temperature(1.0, 3.0, nullptr)); }
        catch (const std::runtime_error&) { host_threw = true; }
        if (host_threw != invalid) fail("Diffusion Tabular fixture missed the Host failure boundary");
        device_table.upload(table.data(), table.size());
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            eos.free_energy_fields[field] = device_table.pointer + field * extent;

        const auto operation = arch::cuda::launch_diffusion_operator(
            fixture.state.view, fixture.output.view, eos, fixture.eos.species,
            fixture.device_grid, config, fixture.workspace, nullptr);
        require_cuda(operation.error, "Tabular diffusion operator latch");
        require_cuda(cudaDeviceSynchronize(), "Tabular diffusion operator sync");
        int status = -1;
        fixture.status.download(&status, 1);
        if (status != (invalid ? 1 : 0))
            fail("Diffusion operator concealed an intermediate EOS failure or failed to reset status");
        const auto output = fixture.output.download();
        for (int i = fixture.grid.Is(); i < fixture.grid.Ie(); ++i)
            if (!std::isfinite(output.eng[fixture.grid.GetIndex(i, 0, 0)]))
                fail("Diffusion fixture did not retain finite operator output");

        const auto timestep = arch::cuda::launch_raw_diffusion_dt(
            fixture.state.view, eos, fixture.eos.species, fixture.device_grid,
            config, fixture.workspace, nullptr);
        require_cuda(timestep.error, "Tabular diffusion dt latch");
        require_cuda(cudaDeviceSynchronize(), "Tabular diffusion dt sync");
        fixture.status.download(&status, 1);
        double dt = 0.0;
        fixture.result.download(&dt, 1);
        if (status != (invalid ? 1 : 0) || !std::isfinite(dt))
            fail("Diffusion dt reduction concealed an intermediate EOS failure or failed to reset status");
        if (!invalid) {
            // An invalid EOS payload is rejected by its status, not a physical
            // timestep reference. Check recovery against the uniform-capacity
            // analytic limit, then bitwise repeatability of two valid runs.
            expect_close("Tabular recovered uniform-capacity dt", dt,
                         fixture.grid.dx1*fixture.grid.dx1/(2.*config.alpha_therm));
            if (previous_dt > 0. && std::bit_cast<std::uint64_t>(dt)
                != std::bit_cast<std::uint64_t>(previous_dt))
                fail("Tabular valid timestep is not repeatable after recovery");
            previous_dt = dt;
        }
    }
}

void verify_tabular_diffusion_failures()
{
    Tabular3DEOSView eos3{};
    eos3.n_rho = eos3.n_T = eos3.n_X = 2;
    eos3.log_rho_max = eos3.log_T_max = eos3.dlog_rho = eos3.dlog_T = 1.0;
    eos3.X_max = eos3.dX = 1.0;
    eos3.target_species_id = -1;
    verify_tabular_diffusion_latch(eos3, 2);
    Tabular4DEOSView eos4{};
    eos4.n_rho = eos4.n_T = eos4.n_A = eos4.n_Z = 2;
    eos4.log_rho_max = eos4.log_T_max = eos4.dlog_rho = eos4.dlog_T = 1.0;
    eos4.A_min = 14.0; eos4.A_max = 15.0; eos4.dA = 1.0;
    eos4.Z_min = 7.0; eos4.Z_max = 8.0; eos4.dZ = 1.0;
    verify_tabular_diffusion_latch(eos4, 4);
}

void compare_operator_state(
    const FluidState& device, const FluidState& host, const Grid& grid,
    std::array<ErrorEnvelope, 8>& envelopes)
{
    static constexpr const char* labels[8] = {
        "operator.rho", "operator.mom_u", "operator.mom_v",
        "operator.mom_w", "operator.eng", "operator.X0",
        "operator.X1", "operator.ENUC"};
    // Frozen on the H100 sm90 envelope. Exact/zero fields stay bitwise; each
    // cancellation-prone field records both an absolute and relative budget.
    static constexpr NumericBudget budgets[8] = {
        {0.0, 0.0, true},
        {2.0e-13, 3.0e-10, false},
        {6.0e-14, 2.0e-10, false},
        {3.0e-14, 2.0e-10, false},
        {3.0e-11, 3.0e-10, false},
        {6.0e-17, 5.0e-13, false},
        {3.0e-17, 3.0e-13, false},
        {0.0, 0.0, true}};
    for (int k = grid.Ks(); k < grid.Ke(); ++k) {
        for (int j = grid.Js(); j < grid.Je(); ++j) {
            for (int i = grid.Is(); i < grid.Ie(); ++i) {
                const int cell = grid.GetIndex(i, j, k);
                const double actual[8] = {
                    device.rho[cell], device.mom_u[cell],
                    device.mom_v[cell], device.mom_w[cell], device.eng[cell],
                    device.X(0, cell), device.X(1, cell),
                    device.enuc_rate[cell]};
                const double expected[8] = {
                    host.rho[cell], host.mom_u[cell], host.mom_v[cell],
                    host.mom_w[cell], host.eng[cell], host.X(0, cell),
                    host.X(1, cell), host.enuc_rate[cell]};
                for (int field = 0; field < 8; ++field)
                    compare_with_budget(labels[field], actual[field],
                                        expected[field], cell, budgets[field],
                                        envelopes[field]);
            }
        }
    }
}

void verify_device_conductivity_and_floor()
{
    DeviceArray<double> result(1);
    conductivity_probe<<<1, 1>>>(result.pointer);
    require_cuda(cudaGetLastError(), "conductivity launch");
    require_cuda(cudaDeviceSynchronize(), "conductivity sync");
    double conductivity = 0.0;
    result.download(&conductivity, 1);
    expect_close("conductivity device", conductivity,
                 std::bit_cast<double>(0x4361a6e6e5986971ULL));

    inverse_dt_floor_probe<<<1, 1>>>(result.pointer);
    require_cuda(cudaGetLastError(), "inverse dt floor launch");
    require_cuda(cudaDeviceSynchronize(), "inverse dt floor sync");
    double floor = 0.0;
    result.download(&floor, 1);
    expect_bits("inverse dt floor", floor, 0x4415af1d78b58c40ULL);

    DeviceArray<double> thresholds(12);
    threshold_probe<<<1, 1>>>(thresholds.pointer);
    require_cuda(cudaGetLastError(), "threshold probe launch");
    require_cuda(cudaDeviceSynchronize(), "threshold probe sync");
    double values[12]{};
    thresholds.download(values, 12);
    expect_bits("rho floor below", values[0], 0x0ULL);
    expect_bits("rho floor exact", values[1], 0x3ff0000000000000ULL);
    expect_bits("rho floor above", values[2], 0x3ff0000000000000ULL);
    const double positive_coefficients[]{std::nextafter(1.e-12, 0.), 1.e-12,
        std::nextafter(1.e-12, std::numeric_limits<double>::infinity())};
    for (int index=0; index<3; ++index)
        expect_close("positive transport remains active", values[3+index],
                     .01*.01/(2.*positive_coefficients[index]));
    expect_bits("inverse dt below floor", values[6], 0x4415af1d78b58c40ULL);
    expect_bits("inverse dt exact floor", values[7], 0x4415af1d78b58c40ULL);
    if (!(values[8] < std::bit_cast<double>(0x4415af1d78b58c40ULL)))
        fail("inverse dt above floor was clamped");
    expect_bits("Cv floor below", values[9],
                std::bit_cast<std::uint64_t>(-1.0e-12));
    expect_bits("Cv floor exact", values[10],
                std::bit_cast<std::uint64_t>(-1.0e-12));
    expect_bits("Cv floor above", values[11],
                std::bit_cast<std::uint64_t>(-std::nextafter(
                    1.0e-12, std::numeric_limits<double>::infinity())));
}

void verify_device_operator_and_dt()
{
    DeviceFixture fixture;
    SpeciesManager species = make_species();
    IdealGas eos(1.37, species);
    const bool route_flags[4][3] = {
        {true, false, false}, {false, true, false},
        {false, false, true}, {true, true, true}};
    std::array<ErrorEnvelope, 8> operator_envelopes{};
    for (const auto& route : route_flags) {
        const SimConfig config = make_config(route[0], route[1], route[2]);
        FluidState host_operator;
        host_operator.Preallocate(fixture.grid.GetTotalSize());
        host_operator.InitSpecies(2);
        std::fill(host_operator.enuc_rate.begin(), host_operator.enuc_rate.end(), -777.0);
        DiffFlux::compute_diffusion_operator(
            fixture.host_state, host_operator, eos, fixture.grid, config);

        FluidState sentinel = host_operator;
        std::fill(sentinel.rho.begin(), sentinel.rho.end(), -101.0);
        std::fill(sentinel.mom_u.begin(), sentinel.mom_u.end(), -102.0);
        std::fill(sentinel.mom_v.begin(), sentinel.mom_v.end(), -103.0);
        std::fill(sentinel.mom_w.begin(), sentinel.mom_w.end(), -104.0);
        std::fill(sentinel.eng.begin(), sentinel.eng.end(), -105.0);
        std::fill(sentinel.enuc_rate.begin(), sentinel.enuc_rate.end(), -777.0);
        std::fill(sentinel.mass_fractions.begin(), sentinel.mass_fractions.end(), -106.0);
        fixture.output.upload(sentinel);
        fixture.flux.upload(sentinel);

        const auto launch = arch::cuda::launch_diffusion_operator(
            fixture.state.view, fixture.output.view, fixture.eos,
            fixture.eos.species, fixture.device_grid,
            DiffFlux::make_diffusion_config_view(config), fixture.workspace, nullptr);
        require_cuda(launch.error, "diffusion operator");
        if (launch.kernels_launched != 2
            || launch.effect != arch::cuda::DiffusionWriteEffect::InteriorWritten)
            fail("operator launch/effect contract drifted");
        require_cuda(cudaDeviceSynchronize(), "operator sync");
        int status = -1;
        fixture.status.download(&status, 1);
        if (status != 0) fail("valid operator reported invalid runtime status");
        compare_operator_state(fixture.output.download(), host_operator,
                               fixture.grid, operator_envelopes);

        std::vector<FluidVector> host_flux(fixture.grid.GetTotalSize());
        std::vector<double> host_species_flux(2 * fixture.grid.GetTotalSize(), 0.0);
        DiffFlux::compute_fluxes(fixture.host_state, eos, fixture.grid, config,
                                 host_flux, host_species_flux, 0);
        const FluidState device_flux = fixture.flux.download();
        const int face = fixture.grid.GetIndex(fixture.grid.Is() + 5, 0, 0);
        expect_close("face u", device_flux.mom_u[face], host_flux[face].mom_u);
        expect_close("face v", device_flux.mom_v[face], host_flux[face].mom_v);
        expect_close("face w", device_flux.mom_w[face], host_flux[face].mom_w);
        expect_close("face e", device_flux.eng[face], host_flux[face].eng);
        expect_close("face x0", device_flux.X(0, face), host_species_flux[face]);
        expect_close("face x1", device_flux.X(1, face),
                     host_species_flux[fixture.grid.GetTotalSize() + face]);
    }

    const SimConfig config = make_config(true, true, true);
    const auto dt_launch = arch::cuda::launch_raw_diffusion_dt(
        fixture.state.view, fixture.eos, fixture.eos.species,
        fixture.device_grid, DiffFlux::make_diffusion_config_view(config),
        fixture.workspace, nullptr);
    require_cuda(dt_launch.error, "raw dt");
    if (dt_launch.kernels_launched != 2) fail("raw dt launch count drifted");
    require_cuda(cudaDeviceSynchronize(), "raw dt sync");
    int status = -1;
    fixture.status.download(&status, 1);
    if (status != 0) fail("valid dt reported invalid runtime status");
    double raw_dt = 0.0;
    fixture.result.download(&raw_dt, 1);
    expect_close("raw dt device", raw_dt,
        DiffFlux::adaptive_dt_diff(
            fixture.host_state, eos, fixture.grid, config, 1.0));

    static constexpr const char* labels[8] = {
        "rho", "mom_u", "mom_v", "mom_w", "eng", "X0", "X1", "ENUC"};
    for (int field = 0; field < 8; ++field) {
        std::cout << std::setprecision(17)
                  << "C4_OPERATOR_ENVELOPE field=" << labels[field]
                  << " max_abs=" << operator_envelopes[field].maximum_absolute
                  << " abs_cell=" << operator_envelopes[field].absolute_cell
                  << " max_rel=" << operator_envelopes[field].maximum_relative
                  << " rel_cell=" << operator_envelopes[field].relative_cell
                  << '\n';
    }
}

void verify_device_dt_dimensions()
{
    SpeciesManager host_species = make_species();
    IdealGas host_eos(1.37, host_species);
    for (int dimension = 1; dimension <= 3; ++dimension) {
        const Grid grid = make_grid(dimension);
        const FluidState host_state = make_state(grid, 2);
        DeviceStateOwner state(grid.GetTotalSize(), 2);
        state.upload(host_state);
        DeviceArray<double> candidates(static_cast<std::size_t>(
            (grid.Ie() - grid.Is()) * (grid.Je() - grid.Js())
                * (grid.Ke() - grid.Ks())));
        DeviceArray<double> result(1);
        DeviceArray<int> status(1);
        DeviceArray<double> A(2), Z(2), gamma(2), cv(2);
        const double hA[2] = {1.0, 4.0};
        const double hZ[2] = {1.0, 2.0};
        const double hgamma[2] = {1.4, 1.5};
        const double hcv[2] = {3.5, 7.25};
        A.upload(hA, 2); Z.upload(hZ, 2);
        gamma.upload(hgamma, 2); cv.upload(hcv, 2);
        IdealGasView eos{};
        eos.species = {A.pointer, Z.pointer, gamma.pointer, cv.pointer, 2};
        eos.global_gamma = 1.37;
        arch::cuda::DiffusionWorkspaceView workspace{
            {}, candidates.pointer, result.pointer, status.pointer};
        const auto launch = arch::cuda::launch_raw_diffusion_dt(
            state.view, eos, eos.species,
            arch::cuda::make_device_grid_view(grid),
            DiffFlux::make_diffusion_config_view(
                make_config(true, true, true)),
            workspace, nullptr);
        require_cuda(launch.error, "dimension raw dt");
        require_cuda(cudaDeviceSynchronize(), "dimension raw dt sync");
        double actual = 0.0;
        result.download(&actual, 1);
        expect_close("dimension raw dt authority", actual,
                     cartesian_face_dt_reference(host_state, grid, make_config(true, true, true)));
        int runtime_status = -1;
        status.download(&runtime_status, 1);
        if (runtime_status != 0)
            fail("valid dimension raw dt reported invalid status");
    }
}

void verify_preflight_and_master_off()
{
    DeviceFixture fixture;
    SimConfig disabled = make_config(true, true, true);
    disabled.physics.diffusion.use_diffusion = false;
    const auto config = DiffFlux::make_diffusion_config_view(disabled);
    auto result = arch::cuda::launch_raw_diffusion_dt(
        fixture.state.view, fixture.eos, fixture.eos.species,
        fixture.device_grid, config, fixture.workspace, nullptr);
    require_cuda(result.error, "master-off dt");
    if (result.kernels_launched != 0
        || result.resolved_dt != DiffFlux::diffusion_dt_sentinel())
        fail("master-off dt did not return sentinel with zero kernels");
    require_cuda(cudaStreamSynchronize(nullptr), "master-off copy sync");
    double dt = 0.0;
    fixture.result.download(&dt, 1);
    expect_bits("master-off device sentinel", dt, 0x4202a05f20000000ULL);

    result = arch::cuda::launch_diffusion_operator(
        fixture.state.view, fixture.output.view, fixture.eos,
        fixture.eos.species, fixture.device_grid, config, fixture.workspace, nullptr);
    if (result.error != cudaSuccess || result.kernels_launched != 0
        || result.effect != arch::cuda::DiffusionWriteEffect::None)
        fail("master-off operator launched work");

    auto invalid_grid = fixture.device_grid;
    invalid_grid.geometry = 99;
    result = arch::cuda::launch_diffusion_operator(
        fixture.state.view, fixture.output.view, fixture.eos,
        fixture.eos.species, invalid_grid,
        DiffFlux::make_diffusion_config_view(make_config(true, true, true)),
        fixture.workspace, nullptr);
    if (result.error != cudaErrorInvalidValue || result.kernels_launched != 0)
        fail("unsupported geometry silently fell back");

    result = arch::cuda::launch_diffusion_operator(
        fixture.state.view, fixture.state.view, fixture.eos,
        fixture.eos.species, fixture.device_grid,
        DiffFlux::make_diffusion_config_view(make_config(true, true, true)),
        fixture.workspace, nullptr);
    if (result.error != cudaErrorInvalidValue || result.kernels_launched != 0)
        fail("operator input/output alias was accepted");

    result = arch::cuda::launch_diffusion_operator(
        fixture.state.view, fixture.output.view, StellarOverrideProbeEos{},
        fixture.eos.species, fixture.device_grid,
        DiffFlux::make_diffusion_config_view(make_config(true, true, true)),
        fixture.workspace, nullptr);
    require_cuda(result.error, "stellar override status launch");
    require_cuda(cudaDeviceSynchronize(), "stellar override status sync");
    int runtime_status = 0;
    fixture.status.download(&runtime_status, 1);
    if (runtime_status != 1)
        fail("invalid stellar coefficient override was not reported");

    auto shifted = fixture.output.view;
    shifted.rho = fixture.state.view.rho + 1;
    if (!arch::cuda::detail::any_state_storage_alias(
            shifted, fixture.state.view))
        fail("partially overlapping device views escaped alias preflight");

    if (DiffFlux::diffusion_face_spacing(
            DiffFlux::DiffusionGeometry::Unsupported, 1, 0,
            0.01, 0.01, 0.01, 1.0, 1.0) != 0.0)
        fail("unsupported geometry retained a Cartesian-like spacing fallback");

    const auto first = DiffFunction::get_rkl_coeffs(
        DiffFunction::RKLOrder::First, 1, 5);
    result = arch::cuda::launch_first_rkl_stage(
        fixture.state.view, fixture.state.view, fixture.output.view,
        fixture.device_grid, config, first.tilde_mu * 0.23, nullptr);
    if (result.error != cudaSuccess || result.kernels_launched != 0)
        fail("master-off RKL stage launched work");
}

void verify_buffer_initialization()
{
    DeviceFixture fixture;
    FluidState scratch_sentinel = make_state(fixture.grid, 2);
    FluidState next_sentinel = scratch_sentinel;
    std::fill(scratch_sentinel.rho.begin(), scratch_sentinel.rho.end(), -11.0);
    std::fill(scratch_sentinel.enuc_rate.begin(),
              scratch_sentinel.enuc_rate.end(), -12.0);
    std::fill(scratch_sentinel.mass_fractions.begin(),
              scratch_sentinel.mass_fractions.end(), -13.0);
    std::fill(next_sentinel.rho.begin(), next_sentinel.rho.end(), -21.0);
    std::fill(next_sentinel.enuc_rate.begin(), next_sentinel.enuc_rate.end(), -22.0);
    std::fill(next_sentinel.mass_fractions.begin(),
              next_sentinel.mass_fractions.end(), -23.0);
    fixture.output.upload(scratch_sentinel);
    fixture.flux.upload(next_sentinel);

    SimConfig disabled_config = make_config(true, true, true);
    disabled_config.physics.diffusion.use_diffusion = false;
    auto result = arch::cuda::launch_initialize_rkl_stage_buffers(
        fixture.state.view, fixture.output.view, fixture.flux.view,
        DiffFlux::make_diffusion_config_view(disabled_config), nullptr);
    if (result.error != cudaSuccess || result.kernels_launched != 0
        || result.effect != arch::cuda::DiffusionWriteEffect::None)
        fail("master-off RKL buffer initialization launched work");
    expect_bits("master-off buffer scratch", fixture.output.download().rho[0],
                std::bit_cast<std::uint64_t>(scratch_sentinel.rho[0]));
    expect_bits("master-off buffer next", fixture.flux.download().rho[0],
                std::bit_cast<std::uint64_t>(next_sentinel.rho[0]));

    const auto enabled = DiffFlux::make_diffusion_config_view(
        make_config(true, true, true));
    result = arch::cuda::launch_initialize_rkl_stage_buffers(
        fixture.state.view, fixture.output.view, fixture.flux.view,
        enabled, nullptr);
    require_cuda(result.error, "RKL buffer initialization");
    if (result.kernels_launched != 1
        || result.effect
            != arch::cuda::DiffusionWriteEffect::CompleteStateCopied)
        fail("RKL buffer initialization effect drifted");
    require_cuda(cudaDeviceSynchronize(), "RKL buffer initialization sync");

    const FluidState scratch = fixture.output.download();
    const FluidState next = fixture.flux.download();
    for (int cell = 0; cell < fixture.grid.GetTotalSize(); ++cell) {
        expect_bits("buffer scratch rho", scratch.rho[cell],
                    std::bit_cast<std::uint64_t>(fixture.host_state.rho[cell]));
        expect_bits("buffer next rho", next.rho[cell],
                    std::bit_cast<std::uint64_t>(fixture.host_state.rho[cell]));
        expect_bits("buffer scratch ENUC", scratch.enuc_rate[cell],
                    std::bit_cast<std::uint64_t>(
                        fixture.host_state.enuc_rate[cell]));
        expect_bits("buffer next ENUC", next.enuc_rate[cell],
                    std::bit_cast<std::uint64_t>(
                        fixture.host_state.enuc_rate[cell]));
        for (int species = 0; species < 2; ++species) {
            expect_bits("buffer scratch species", scratch.X(species, cell),
                        std::bit_cast<std::uint64_t>(
                            fixture.host_state.X(species, cell)));
            expect_bits("buffer next species", next.X(species, cell),
                        std::bit_cast<std::uint64_t>(
                            fixture.host_state.X(species, cell)));
        }
    }

    result = arch::cuda::launch_initialize_rkl_stage_buffers(
        fixture.state.view, fixture.state.view, fixture.flux.view,
        enabled, nullptr);
    if (result.error != cudaErrorInvalidValue || result.kernels_launched != 0)
        fail("RKL buffer source/destination alias was accepted");
}

void verify_rkl_stages()
{
    DeviceFixture fixture;
    FluidState state_n = fixture.host_state;
    FluidState previous = fixture.host_state;
    FluidState older = fixture.host_state;
    FluidState op_previous = fixture.host_state;
    FluidState op_initial = fixture.host_state;
    FluidState destination = fixture.host_state;
    for (int cell = 0; cell < fixture.grid.GetTotalSize(); ++cell) {
        previous.rho[cell] *= 1.03;
        previous.mom_u[cell] *= 0.97;
        previous.eng[cell] *= 1.01;
        older.rho[cell] *= 0.96;
        older.mom_v[cell] *= 1.04;
        older.eng[cell] *= 0.98;
        op_previous.set(cell, {0.07 + cell * 1e-6, -0.03, 0.02, -0.01, 0.11});
        op_initial.set(cell, {-0.02, 0.01, -0.04, 0.03, 0.05});
        for (int s = 0; s < 2; ++s) {
            op_previous.X(s, cell) = 0.004 * (s + 1);
            op_initial.X(s, cell) = -0.002 * (s + 1);
        }
        destination.enuc_rate[cell] = 123456.0 + cell;
        older.enuc_rate[cell] = 654321.0 + cell;
    }

    DeviceStateOwner d_n(fixture.grid.GetTotalSize(), 2);
    DeviceStateOwner d_previous(fixture.grid.GetTotalSize(), 2);
    DeviceStateOwner d_older(fixture.grid.GetTotalSize(), 2);
    DeviceStateOwner d_op_previous(fixture.grid.GetTotalSize(), 2);
    DeviceStateOwner d_op_initial(fixture.grid.GetTotalSize(), 2);
    DeviceStateOwner d_destination(fixture.grid.GetTotalSize(), 2);
    d_n.upload(state_n); d_previous.upload(previous); d_older.upload(older);
    d_op_previous.upload(op_previous); d_op_initial.upload(op_initial);
    d_destination.upload(destination);
    const auto enabled = DiffFlux::make_diffusion_config_view(
        make_config(true, true, true));

    const auto first = DiffFunction::get_rkl_coeffs(
        DiffFunction::RKLOrder::First, 1, 5);
    auto launch = arch::cuda::launch_first_rkl_stage(
        d_n.view, d_op_previous.view, d_destination.view,
        fixture.device_grid, enabled, first.tilde_mu * 0.23, nullptr);
    require_cuda(launch.error, "first RKL stage");
    if (launch.kernels_launched != 1
        || launch.effect != arch::cuda::DiffusionWriteEffect::InteriorWritten)
        fail("first RKL launch/effect drifted");
    require_cuda(cudaDeviceSynchronize(), "first RKL sync");
    const FluidState first_device = d_destination.download();
    compare_frozen_rkl_state(
        "first", first_device, kFirst, fixture.grid, kFirstRklBudgets);
    const int ghost_cell = fixture.grid.GetIndex(0, 0, 0);
    expect_bits("first RKL leaves ghost rho untouched",
                first_device.rho[ghost_cell],
                std::bit_cast<std::uint64_t>(fixture.host_state.rho[ghost_cell]));
    expect_bits("first RKL leaves ghost ENUC untouched",
                first_device.enuc_rate[ghost_cell],
                std::bit_cast<std::uint64_t>(destination.enuc_rate[ghost_cell]));

    const auto rkl1 = DiffFunction::get_rkl_coeffs(
        DiffFunction::RKLOrder::First, 3, 5);
    d_destination.upload(destination);
    d_older.upload(older);
    launch = arch::cuda::launch_recursive_rkl_stage(
        d_n.view, d_previous.view, d_older.view, d_op_previous.view,
        d_op_initial.view, d_destination.view, fixture.device_grid,
        enabled, rkl1, false, 1.0, true, nullptr);
    require_cuda(launch.error, "RKL1 recursive");
    require_cuda(cudaDeviceSynchronize(), "RKL1 sync");
    compare_frozen_rkl_state(
        "scaled_rkl1", d_destination.download(), kScaledRkl1,
        fixture.grid, kScaledRkl1Budgets);

    const auto rkl2 = DiffFunction::get_rkl_coeffs(
        DiffFunction::RKLOrder::Second, 3, 5);
    launch = arch::cuda::launch_recursive_rkl_stage(
        d_n.view, d_previous.view, d_older.view, d_op_previous.view,
        d_op_initial.view, d_older.view, fixture.device_grid,
        enabled, rkl2, true, 1.0, true, nullptr);
    require_cuda(launch.error, "RKL2 recursive alias");
    require_cuda(cudaDeviceSynchronize(), "RKL2 sync");
    compare_frozen_rkl_state(
        "scaled_rkl2", d_older.download(), kScaledRkl2,
        fixture.grid, kScaledRkl2Budgets);

    constexpr double stage_dt = 0.23;
    d_destination.upload(destination);
    d_older.upload(older);
    launch = arch::cuda::launch_recursive_rkl_stage(
        d_n.view, d_previous.view, d_older.view, d_op_previous.view,
        d_op_initial.view, d_destination.view, fixture.device_grid,
        enabled, rkl1, false, stage_dt, false, nullptr);
    require_cuda(launch.error, "RKL1 unscaled recursive");
    require_cuda(cudaDeviceSynchronize(), "RKL1 unscaled sync");
    compare_frozen_rkl_state(
        "unscaled_rkl1", d_destination.download(), kUnscaledRkl1,
        fixture.grid, kUnscaledRkl1Budgets);

    d_destination.upload(destination);
    d_older.upload(older);
    launch = arch::cuda::launch_recursive_rkl_stage(
        d_n.view, d_previous.view, d_older.view, d_op_previous.view,
        d_op_initial.view, d_destination.view, fixture.device_grid,
        enabled, rkl2, true, stage_dt, false, nullptr);
    require_cuda(launch.error, "RKL2 unscaled recursive");
    require_cuda(cudaDeviceSynchronize(), "RKL2 unscaled sync");
    compare_frozen_rkl_state(
        "unscaled_rkl2", d_destination.download(), kUnscaledRkl2,
        fixture.grid, kUnscaledRkl2Budgets);

    launch = arch::cuda::launch_first_rkl_stage(
        d_n.view, d_op_previous.view, d_n.view, fixture.device_grid,
        enabled, first.tilde_mu * 0.23, nullptr);
    if (launch.error != cudaErrorInvalidValue || launch.kernels_launched != 0)
        fail("RKL read/write alias preflight accepted invalid buffers");

    FluidState zero_state = fixture.host_state;
    FluidState zero_increment = fixture.host_state;
    for (int cell = 0; cell < fixture.grid.GetTotalSize(); ++cell) {
        zero_state.set(cell, {1.0, 0.0, 0.0, 0.0, 0.0});
        zero_state.X(0, cell) = 0.0;
        zero_state.X(1, cell) = 1.0;
        zero_state.enuc_rate[cell] = 700000.0 + cell;
        zero_increment.set(cell, {});
        zero_increment.X(0, cell) = 0.0;
        zero_increment.X(1, cell) = 0.0;
        zero_increment.enuc_rate[cell] = -1.0;
    }
    d_n.upload(zero_state);
    d_op_previous.upload(zero_increment);
    d_destination.upload(zero_state);
    launch = arch::cuda::launch_first_rkl_stage(
        d_n.view, d_op_previous.view, d_destination.view,
        fixture.device_grid, enabled, first.tilde_mu * 0.23, nullptr);
    require_cuda(launch.error, "zero RKL stage");
    require_cuda(cudaDeviceSynchronize(), "zero RKL sync");
    const FluidState zero_device = d_destination.download();
    for (int i = fixture.grid.Is(); i < fixture.grid.Ie(); ++i) {
        const int cell = fixture.grid.GetIndex(
            i, fixture.grid.Js(), fixture.grid.Ks());
        expect_bits("zero RKL rho", zero_device.rho[cell],
                    std::bit_cast<std::uint64_t>(1.0));
        expect_bits("zero RKL mom_u", zero_device.mom_u[cell], 0);
        expect_bits("zero RKL mom_v", zero_device.mom_v[cell], 0);
        expect_bits("zero RKL mom_w", zero_device.mom_w[cell], 0);
        expect_bits("zero RKL eng", zero_device.eng[cell], 0);
        expect_bits("zero RKL X0", zero_device.X(0, cell), 0);
        expect_bits("zero RKL X1", zero_device.X(1, cell),
                    std::bit_cast<std::uint64_t>(1.0));
        expect_bits("zero RKL ENUC", zero_device.enuc_rate[cell],
                    std::bit_cast<std::uint64_t>(zero_state.enuc_rate[cell]));
    }
}

} // namespace

int main()
{
    static_assert(std::is_standard_layout_v<DiffFlux::DiffusionConfigView>);
    static_assert(std::is_trivially_copyable_v<DiffFlux::DiffusionConfigView>);
    static_assert(std::is_standard_layout_v<arch::cuda::DiffusionWorkspaceView>);
    static_assert(std::is_trivially_copyable_v<arch::cuda::DiffusionWorkspaceView>);

    verify_budget_comparator_special_values();
    verify_frozen_host_authority();
    verify_device_conductivity_and_floor();
    verify_device_operator_and_dt();
    verify_tabular_diffusion_failures();
    verify_device_dt_dimensions();
    verify_preflight_and_master_off();
    verify_buffer_initialization();
    verify_rkl_stages();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
