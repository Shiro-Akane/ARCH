# 历史生成式自定义网络与 KLU 兼容性

这是生成器 v3 的历史 CPU 证据，不是当前 CUDA 发布候选的合格证明。
当前情况见[模块摘要](../../README.zh-CN.md)。

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

> CPU 状态：31、150、200 核 pynucastro package 的生成、共存、dispatch、生成式 RHS/Jacobian、SparseKLU factor/refactor 及单步 driver smoke test 均通过；不据此声明长期物理资格。

本记录使用用户自定义网络名，而不是预留固定 `sn160` 槽位。三个 package 生成到互为同级的文件夹，并在同一次 CMake 配置中注册；每份 `.par` 只选择一个运行时名称，因此它们不会互相替换，也不会覆盖任何 `aprox*`/`iso*` 内置网络。

## 环境与 package

测试使用 GCC 13.3.0、`p311` 环境中的 Python 3.11.15、pynucastro 2.12.0 和 SuiteSparse 7.13.0；工作树基于 `affde827fcbf317382ed45372912b562652a71c5`。三个 custom network 的组合构建使用 CPU backend、Debug 配置和 `ARCH_ENABLE_OPENMP=OFF`，硬件为 x86_64 WSL2 下的 Intel Core i7-10700。加入 inert-species 防呆后，全部 package 均由 `GenerateNetwork.py` 版本 3 重新生成。

| 运行时 ID | 核素数 | ReacLib 反应数 | Recipe SHA256 |
| --- | ---: | ---: | --- |
| `custom:audit31` | 31 | 229 | `153e21acf3e265a9ffcacd60e6ebd8f9263f5409cb16eda4df64854236884607` |
| `custom:audit150` | 150 | 1416 | `a6b2fbb68c92aac0c44734bb01cd66091076576cbdb88cb58b0bb4994933ba4e` |
| `custom:audit200` | 200 | 1965 | `882d94567940e144f4d37a1128af8c0472ce89a8537ac90b385677822de9681c` |

这些 recipe 是围绕广义稳定谷构造的压力测试定义，不是已发表的 `sn160` 类物理网络。生成的 C++ package 属于构建产物，不纳入仓库。

另一份单核素、零反应的 `--check` probe 将请求核素保留为一个 inert species，证明异常的非连通 `NUCLEI` recipe 不会被静默缩减为空网络。

## 求解器结果

在 `rho = 1e7 g cm^-3`、`T = 3e9 K`、C12/O16 质量分数各为 0.5 的状态下，生成的 RHS 和 Jacobian 进入 `SparseMatrixData` 与 KLU。每个矩阵先针对制造解完成 factor/solve，再在 `1.01 T` 重算并沿用相同 symbolic pattern 执行 refactor。

| 核素数 | 质量 RHS 相对残差 | 首次 KLU 误差 | refactor 误差 |
| ---: | ---: | ---: | ---: |
| 31 | `2.80e-17` | `0.00` | `4.44e-16` |
| 150 | `1.27e-16` | `0.00` | `6.66e-16` |
| 200 | `1.46e-16` | `8.88e-16` | `2.22e-16` |

三份 Helmholtz `BurnOneZone` 使用 BE_NR 与 `linear_solver = Auto`，各推进一步到 `1e-16 s`，均正常退出并选择 `N = 32`、`151`、`201` 的 SparseKLU。显式 DenseLU 按设计拒绝 31 核算例，从而维持其不超过 30 核的专用契约。另一份关闭 KLU 的构建在同一大型网络的 dispatch 阶段即拒绝 `Auto`，没有进入积分器。

pynucastro 2.12 会为每个结构上不存在的核素对生成 `jac.set(..., 0.0)`。生成器版本 3 只删除这些编译期字面零调用，绝不会按某个状态下恰为零的运行值过滤。保留的 species-block pattern 分别为 423、2363、3223 项，而不是 31、150、200 的平方；加入温度行列后的完整 ODE Jacobian 分别为 486、2664、3624 项，从而保留 KLU symbolic 复用并恢复预期的稀疏存储。

## 重新生成并编译

~~~bash
CUSTOM_ROOT=/tmp/arch-custom-network-validation

conda run -n p311 python tools/network/GenerateNetwork.py \
  validation/network/inputs/audit31.py \
  --check --custom-root "$CUSTOM_ROOT"
conda run -n p311 python tools/network/GenerateNetwork.py \
  validation/network/inputs/audit31.py \
  --custom-root "$CUSTOM_ROOT"

# 对 audit150.py 和 audit200.py 重复上述步骤，再同时配置三个网络。
cmake -S . -B build-network-validation \
  -DARCH_ENABLE_KLU=ON \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY=/tmp/arch-network-validation-bin \
  -DARCH_CUSTOM_NETWORK_ROOT="$CUSTOM_ROOT" \
  -DARCH_CUSTOM_NETWORKS="audit31;audit150;audit200"
cmake --build build-network-validation --parallel 1
~~~

上述命令选择实际测试使用的 `p311` conda 环境；也可使用任何含对应 pynucastro 版本的 Python 3.11 环境。`--custom-root` 是审计选项，常规用户生成位置仍为 `src/physics/network/custom/`。

一次性 RHS/KLU/refactor harness 与生成 package 不提交到仓库。上表和 [metrics.csv](metrics.csv) 保留本轮审计证据；这里的命令复现 package 生成、registry 自动发现和编译，不新增永久 test target。

## 范围标签

- **已验证：**安全 package 命名、非连通核素保留、不覆盖内置网络、三 package 共存、CMake 选择、`.par` dispatch、生成式 RHS/Jacobian、KLU 关闭时 fail-fast、质量 RHS 残差、KLU factor/refactor，以及短程 driver/I/O 执行。
- **不属于物理资格：**screening 选择、反应完整性、容差、热演化、弱反应能量导数和长期丰度轨迹仍由各用户网络自行验证。
- **规模边界：**稀疏数值采用 CSC 存储，但当前 entry-to-slot 查询仍分配 `N*N` 个整数；审计覆盖到 200 核素，不声明网络规模无上限。
- **待完成：**CUDA 稀疏后端一致性；CPU KLU 结果不自动代表 cuDSS 兼容。

机器可读结果保留在 [metrics.csv](metrics.csv)。
