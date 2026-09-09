# 第三方来源与说明

英文原文：[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。本译文不替代上游许可证，也不授予额外权利。

[`LICENSE`](LICENSE) 中的 MIT 许可证适用于 ARCH 自有内容。由第三方科学软件改写的文件保留其上游来源与条款。

## Frank Timmes 科学软件

ARCH 包含 Frank Timmes 所发布软件的 C++ 适配：

| ARCH 区域 | 直接上游来源 | ARCH 贡献 |
| --- | --- | --- |
| `src/physics/network/{iso7,aprox13,aprox19,aprox21}/` | [Timmes 反应网络页面](https://cococubed.com/code_pages/burn.shtml)中的 `public_iso7.f90`、`public_aprox13.f90`、`public_aprox19.f90` 和 `public_aprox21.f90` | C++ 策略接口、通用求解器耦合、生成导数和 CPU/GPU 可移植组织 |
| `src/physics/nse/nse_solver.h` | [Timmes NSE 页面](https://cococubed.com/code_pages/nse.shtml)中的 `public_nse` | 编译期网络耦合、数值保护、小网络处理和能量闭合 |
| `src/physics/eos/HelmEos.h` | [Timmes EOS 页面](https://cococubed.com/code_pages/eos.shtml)中的 Helmholtz EOS 包 | C++ EOS 策略、严格表加载、状态耦合和诊断 |
| `EOS_toolkit/tables/helmholtz/helm_table.dat` | 项目下载的 `helmholtz.tar.xz` 中的 `helm_table.dat` | 仅 Git LFS 打包；表数据不是 ARCH 自有作品 |

Timmes 下载页面请求在使用这些代码、代码片段或修改版本时引用相关文献；反应网络页面也欢迎就集成到其他软件的工作联系作者。原作者 Frank X. Timmes 已明确授权可无限制地自由使用和重新分发这些材料：“my license is whatever i post on cococubed is free to use in any manner one wishes. attribution is nice, but not required.”（我发布在 cococubed 上的所有内容均可按任何意愿自由使用。保留署名很好，但并非强制要求。）

这项明确授权彻底解决了本项目中捆绑的 Timmes 衍生材料的重新分发与许可顾虑。出于学术礼仪并尊重作者的贡献，我们仍保留了来源署名和上游说明。

实现和验证细节见 [`docs/physics/TimmesNetworks.zh-CN.md`](docs/physics/TimmesNetworks.zh-CN.md)。

## 外部 Shen EOS 表与 EOSdriver 兼容格式

ARCH 的原生核物质表读取器由 ARCH 自行实现数据格式读取，没有复制或改写 EOSmaker/EOSdriver 例程。读取器代码与表数据分别记录来源；支持某种表格式不会将表数据的所有权或许可转移给 ARCH。

作者的 [Shen EOS Zenodo 归档，记录 3612487](https://zenodo.org/records/3612487)，在其[记录元数据](https://zenodo.org/api/records/3612487)中声明 `cc-by-4.0`。该归档包含 EOS2 和 EOS4 的 `.tab`、`.t00`、`.yp0` 表，共六个 ZIP 压缩包。这些特定归档文件可按 [Creative Commons 署名 4.0 国际许可协议](https://creativecommons.org/licenses/by/4.0/)重新分发和改编，包括商业用途。须保留已提供的作者署名、来源/DOI、版权、许可和免责声明，提供许可链接，并标明修改及保留既有修改标记；不得暗示作者背书，也不得施加与许可相冲突的额外限制。与 ARCH 的 MIT 代码一同打包时，表数据仍保留其自身许可。

ARCH 在 `EOS_toolkit/tables/baryon/` 通过 Git LFS 随附该归档中未修改的 `eos2.tab` 和 `eos4.tab` 主表成员。署名为 H. Shen、F. Ji、J. N. Hu、K. Sumiyoshi，*Equation of state for simulations of core-collapse supernovae and neutron-star mergers*（2020），[DOI: 10.5281/zenodo.3612487](https://doi.org/10.5281/zenodo.3612487)。只执行了解压和 LFS 打包，没有修改数据字节；精确大小与 SHA-256 校验值见[运行时表指南](EOS_toolkit/README.zh-CN.md#原始-shen-数据)。ARCH 在内存中完成的组件补齐与固定能量基准处理不改变这些分发的来源文件。零温 `.t00`、零电荷 `.yp0` 辅助产品不随附。

上述许可适用于已识别的归档，不会自动覆盖所有 Shen 版本或第三方加工后的表。[StellarCollapse EOS 页面](https://stellarcollapse.org/equationofstate.html)对其代码和表声明了署名、非商业性使用和相同方式共享条件。因此，重新分发 EOSdriver 兼容的 HShen HDF5 表时，须针对具体加工文件及版本核对适用条款与说明，包括制作过程中加入的其他贡献。原始 Zenodo 归档的 CC BY 4.0 不会自动替代这些独立条款。加工后的 HShen HDF5 表仅进行格式兼容性调查，不随 ARCH 分发。

## AMReX-Astro Microphysics

`src/physics/diffusionCoe/diffusion_math.hpp` 是对 AMReX-Astro Microphysics [`conductivity/stellar/actual_conductivity.H`](https://github.com/AMReX-Astro/Microphysics/blob/6fb41b5f7475b42a06eb5b09ff0520c9f08aa7f0/conductivity/stellar/actual_conductivity.H) 的框架无关 C++ 适配，审计基准提交为 `6fb41b5f7475b42a06eb5b09ff0520c9f08aa7f0`。ARCH 提供可调用接口和 EOS/核素集成；传导公式与拟合常数遵循上游实现。

保留的上游许可证位于 [`LICENSES/AMReX-Astro-Microphysics.txt`](LICENSES/AMReX-Astro-Microphysics.txt)。源码重新分发必须保留其版权声明、条件和免责声明；二进制重新分发必须在随附文档或其他材料中重现这些内容。

## SuiteSparse KLU 稀疏求解器

ARCH 可获取固定的 SuiteSparse v7.13.0，并静态链接 KLU 及其最小依赖 BTF、AMD、COLAMD 和 SuiteSparse_config。KLU 与 BTF 使用 LGPL-2.1-or-later；AMD、COLAMD 与 SuiteSparse_config 使用 BSD-3-Clause。保留的组件说明见 [`LICENSES/SuiteSparse-KLU.txt`](LICENSES/SuiteSparse-KLU.txt)，完整 LGPL-2.1 文本见 [`LICENSES/LGPL-2.1.txt`](LICENSES/LGPL-2.1.txt)。源码和二进制重新分发必须满足对应上游条款；ARCH 的 MIT 条款不会重新许可这些组件。

## pynucastro 生成网络与核数据

ARCH 在 [`tools/network/`](tools/network/) 中提供生成配方和可移植适配器。维护中的 [`audit31` 与 `weak_urca` 验证网络](validation/network/README.zh-CN.md)使用 pynucastro 2.12.0，由用户在本地生成网络包；ARCH 的适配工作不会将输出中的上游模板或核数据变成 ARCH 自有内容。

pynucastro 2.12.0 使用其 [BSD-3-Clause 许可证](https://raw.githubusercontent.com/pynucastro/pynucastro/2.12.0/LICENSE)。SimpleCxx 输出包含上游模板，其中 `amrex_bridge.H` 的源码注释注明了 AMReX 和 Microphysics 的改编来源。重新分发生成包时，应保留这些来源及适用条款，包括 pynucastro 许可证和相关 [AMReX](https://raw.githubusercontent.com/AMReX-Codes/amrex/development/LICENSE)、Microphysics 说明。上文已有的 Microphysics 说明针对 ARCH 的传导适配，并不统一覆盖所有生成文件。源码分发须保留适用的版权声明、条件和免责声明；二进制分发须在随附材料中重现这些内容。pynucastro 的[引用指南](https://pynucastro.github.io/pynucastro/citing.html)请求引用其 2.0 论文和 Zenodo 软件记录。

速率数据有各自的科学来源。`audit31` 使用 [JINA ReacLib 数据库](https://reaclib.jinaweb.org/index.php)，该数据库建议的引用为 Cyburt 等人，*ApJS* 189, 240 (2010)。`weak_urca` 使用 pynucastro 随包提供、来自 [Suzuki、Toki 和 Nomoto，*ApJ* 817, 163 (2016)](https://doi.org/10.3847/0004-637X/817/2/163) 的 Na-23/Ne-23 电子俘获和 β 衰变表。pynucastro 的[第三方数据指南](https://pynucastro.github.io/pynucastro/sources.html)列出了这些来源、Suzuki 作者数据页面，以及核属性和配分函数的参考文献。重新分发网络时，应一并保留所选速率、表数据来源和相关引用。本说明不将这些科学数据统一归入 ARCH 的 MIT 许可证或某一软件许可证。

## NVIDIA cuDSS

cuDSS 是可选、独立安装的 NVIDIA 稀疏求解器后端，ARCH 源码发行包不捆绑其二进制文件。本轮核阅的集成使用 0.8 API 和 `nvidia-cudss-cu12` 0.8.0.10 软件包。安装步骤见 NVIDIA 的 [cuDSS 指南](https://docs.nvidia.com/cuda/cudss/getting_started.html)。

cuDSS 受 [NVIDIA Math Libraries SDK 许可协议](https://docs.nvidia.com/cuda/cudss/license.html)约束，不使用 ARCH 的 MIT 许可证。重新分发 SDK 组件须遵循对应版本软件包随附的协议和完整说明。本轮核阅软件包的 `LICENSE.txt` 还包含 AMD/COLAMD、METIS、fmt 和 HSL 说明，应保留该版本的完整说明，而不只摘录 NVIDIA 协议部分。

## 归属目录中的 ARCH 支撑代码

请注意，文件位于某个归属目录中，并不自动意味着它是第三方作者的身份。例如，`timmes_common/Dual.h`、`RatePair.h` 和 `TimmesNetworkSupport.h` 是完全由 ARCH 自行编写的支撑层，用于包裹 Timmes 派生方程；它们的具体文件头已经非常明确地说明了这一边界。同样地，除非文件头或本声明中有特别指出，否则其他任何 ARCH 模块均不作任何外部来源声明。
