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

Timmes 下载页面要求在使用这些代码、代码片段或修改版本时引用相关文献并联系作者。这些页面没有声明标准 SPDX 软件许可证。因此本项目不主张将 Timmes 派生内容重新许可为 ARCH 的 MIT 许可证。维护者应保留来源归属，并在公开发布前确认适用的重新分发条款。

实现和验证细节见 [`docs/physics/TimmesNetworks.zh-CN.md`](docs/physics/TimmesNetworks.zh-CN.md)。

## AMReX-Astro Microphysics

`src/physics/diffusionCoe/diffusion_math.hpp` 是对 AMReX-Astro Microphysics [`conductivity/stellar/actual_conductivity.H`](https://github.com/AMReX-Astro/Microphysics/blob/6fb41b5f7475b42a06eb5b09ff0520c9f08aa7f0/conductivity/stellar/actual_conductivity.H) 的框架无关 C++ 适配，审计基准提交为 `6fb41b5f7475b42a06eb5b09ff0520c9f08aa7f0`。ARCH 提供可调用接口和 EOS/核素集成；传导公式与拟合常数遵循上游实现。

保留的上游许可证位于 [`LICENSES/AMReX-Astro-Microphysics.txt`](LICENSES/AMReX-Astro-Microphysics.txt)。源码重新分发必须保留其版权声明、条件和免责声明；二进制重新分发必须在随附文档或其他材料中重现这些内容。

## 归属目录中的 ARCH 支撑代码

文件位于归属目录中并不自动表示第三方作者身份。例如 `timmes_common/Dual.h`、`RatePair.h` 和 `TimmesNetworkSupport.h` 是围绕 Timmes 派生方程编写的 ARCH 自有支撑层，文件头已明确说明边界。除非文件头或本说明指出，其他 ARCH 模块不声明外部来源。
