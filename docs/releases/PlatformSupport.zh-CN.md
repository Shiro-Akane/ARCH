# 平台支持

[English](PlatformSupport.md) · [构建](../guides/Build.zh-CN.md) · [功能](../Features.zh-CN.md) · [Studio](../guides/Studio.zh-CN.md)

ARCH 支持 Linux／WSL2 和原生 Windows 11 x64。推荐使用 Linux 进行主要科学计算。Windows 在已支持的科学范围内提供相同的 CPU／CUDA CLI 与 Studio 工作流程，但可能仍有平台相关的不稳定性和缺陷。

## 工具链

| 组件 | Linux／WSL2 | Windows 11 x64 |
| --- | --- | --- |
| CPU | GCC、OpenMP、Ninja 和 CMake | GCC/UCRT64 与相同 ABI 的 HDF5/KLU |
| CUDA | NVCC 与兼容的 Host 编译器 | NVCC/MSVC、Windows SDK 与相同 MSVC ABI 的依赖 |
| Studio | 原生 Node 24+、Electron 和图形会话；WSL 使用 WSLg | 原生 x64 Node 24+ 和 Electron |
| 运行终端 | `xterm` 和 `flock` | Windows Terminal 与原生进程助手 |
| 构建目录 | Linux 原生缓存和依赖 | 分开的 GCC/UCRT64 与 MSVC/NVCC 缓存和依赖 |

Windows CUDA 参考配置使用 NVCC 12.9.86、MSVC 19.44.35229（工具集 14.44）、SDK 10.0.26100、CMake 3.31.11、HDF5 1.14.6、SuiteSparse 7.13.0、可选 cuDSS 0.8，以及 RTX 3060 Ti（SM86）。MSVC 14.38 无法通过 ARCH 的 IEEE NaN 检查，此构建方式应使用已测试的 14.44 工具集。NVIDIA 的 CUDA 12.9 编译器表列出 193x，因此 14.44 组合属于额外实测配置。Linux CUDA 参考配置使用 NVCC 12.3/GCC 12。安装命令与依赖设置见[构建指南](../guides/Build.zh-CN.md)。

## 共用功能

两平台均提供 CPU/OpenMP、`cpu`／`cuda`／`auto` 选择，以及相同的流体、几何、AMR、EOS、扩散、反应网络和引力实现，使用相同的 HDF5 输出格式，各平台支持 CPU/CUDA 后端之间的重启。可组合的模型与方法见[功能清单](../Features.zh-CN.md)。

Studio 包含模型发现、参数编辑、Configure/Build、初始场与初始 AMR 预览、Run/Restart，以及只读 Plotfile 元数据、原始采样、原生单元查询和显示总览。桌面包共用 renderer、Host 与打包流程。即使所选执行物支持 CUDA，初始预览仍通过 CPU Setup/Init 生成。

## 平台限制

- MSVC/CUDA 的完整依赖与动态加载新鲜度仍显示 `unknown`；Build 可以生成执行物，同时保留依赖覆盖未知的状态。Studio 单独记录可获得的输入证据。
- Windows Run/Restart 使用配置好的 Windows Terminal，独立控制台 fallback 不属于受支持的启动方式。
- Windows Preview 使用提交内存和 Job 用户态 CPU 时间；Linux 使用地址空间和用户态加内核态 CPU 限额。请求限额遵循各平台的明确策略。
- 原生 GNU LTO 的构建与保留目录需使用 ASCII 路径。运行时 UTF-8 和带空格路径已有测试；UNC／网络文件系统和任意长路径不在已测试范围内。
- 某些安装环境首次加载程序时，元数据请求可能超过短超时。请检查工具链及 DLL 路径，再在程序加载后重试。
- 桌面包是便携目录；Core、工具链／DLL 和终端需另行安装，暂不提供签名安装器或自动更新。

MPI、AMD/HIP 和 Apple Metal 尚不是受支持后端。已实现物理不包含通用辐射输运。Jeans/JENS 的完整 GPU 演化、一般 EOS 适用性及耦合模型限制见[参考手册](../Reference.zh-CN.md)和[验证](../../validation/README.zh-CN.md)。

## v1.4.0 验证依据

v1.4.0 的平台对比包含 142 个原生科学 CTest、九组分辨率中的 37 个标准后端算例、八条 CPU/CUDA AMR 重启路线及九项比较，以及实际 Studio GPU Run/Restart/Plotfile 读回。Linux 覆盖 72 个 CPU CTest、12 个相关 CUDA／fixture 检查和 453 个 Studio 测试；Windows 覆盖 95 个 Studio 测试与七个链接证据测试。Linux 与 Windows 便携包使用 Electron 44.9.1。

输入、源码身份、各项结果和失败记录见 [v1.4.0 平台记录](https://github.com/Shiro-Akane/ARCH/blob/v1.4.0/docs/histroy/build/WindowsNativeCudaQualification-20261027.json)。各物理模块的参考问题及误差预算见[科学验证](../../validation/README.zh-CN.md)。
