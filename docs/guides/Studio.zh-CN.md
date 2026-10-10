# ARCH Studio：本地配置、预览与运行

[English](Studio.md) · [首页](../../README.zh-CN.md) · [Studio 状态](../../studio/STATUS.md)

Studio 是可选的桌面组件，Core 的 CPU／CUDA 构建与运行可独立使用。它用于编辑 `.par`、核对初始场与网格、编译已注册的模型，以及准备本机运行和续算。Core 在 Linux/WSL 编译和运行；这一阶段按 Linux 桌面交付。

## 构建并呼出界面

环境需准备 Linux、Node 24+ 与图形环境；运行终端另需 `xterm` 和 `flock`。在 ARCH 根目录下执行：

```bash
cmake --preset studio-cpu-release -DARCH_STUDIO_NODE=/absolute/path/to/node
cmake --build build-studio-cpu --parallel 2
build-studio-cpu/bin/arch-studio --project "$PWD" --binary build-studio-cpu/bin/ARCH --case Sod --config simulation/Sod/Sod.par
```

Studio 构建选项默认关闭，普通 Core 编译不会安装网页依赖。开启后，CMake 将根据锁定的依赖清单准备 Linux 桌面运行时与生产资源；首次运行需要网络连接与磁盘空间。生成的入口依赖当前源码目录。若已有 Studio 开发环境，也可直接运行 `studio/desktop/arch-studio`。界面会在独立窗口中打开，连接端口由程序内部管理。

可以通过 `--source /path/to/case.cpp` 指定已注册源码，或仅指定 `--project` 后在窗口中选择模型与 `.par`。指定源码时，模型固定为其唯一编译注册项；切换模型请关闭项目，再打开对应源码。源码身份尚未匹配时，界面显示待确认状态。模型发现请求失败时，可点击 `Retry model discovery` 在当前构建上重试；工作副本保持。若源码已修改或尚未编译，请先执行 Configure/Build；选中源码并不代表旧二进制已包含该修改。工作目录、选中的 Core、模型和参数文件将一并显示。Host 仅使用批准的构建配置；GUI 的受控 CPU 配置默认为 `build-studio-cpu`。

## 日常操作

1. 选择 Core 与已注册模型，再打开对应的 `.par`。确认界面显示的模型名与完整参数文件名。
2. 展开 Grid、EOS、Network、Gravity、Diffusion、Runtime 等参数分组。开启相应功能后再编辑其条件参数；无效值与缺项保持可见，不能由默认值掩盖。
3. 基于修改后的工作副本可生成初始 Preview。重型模型首次生成可能较慢；后续修改沿用受控 Preview Session。注意预览不会自动保存或启动模拟。
4. 需要检查细化位置时请求 Initial AMR，界面将显示实际构建的层级与资源估算。达到请求限额时会提示限制，资源提示不等于预测 MPI 作业会 OOM。
5. 源码变动后请显式执行 Configure/Build，并在输出面板中查看状态。使用 Save/Save As 写入参数文件；已有文件发生变动或覆盖时需明确处理。
6. 点击 Run/Restart 使用已关联且保存的 `.par`，核对二进制与输出目录后确认启动。计算将在独立终端中运行，关闭窗口不会取消已启动的作业。Stop 仅针对程序记录的本次作业。

## 初始图与参数

坐标轴、场值/色标分别选择 linear/log，并可调整范围。log 图只能显示正值：零值和负值必须单独处理或标示，不能改写原数据，也不能把零描述为负数。Initial AMR 图层与场图必须来自同一模型、配置和构建身份。

所有 Core 量都按 CGS；长度 `cm`，密度 `g/cm³`，压力 `erg/cm³`，温度 `K`，速度 `cm/s`，角度 `rad`。标准参数说明来自 Core。custom 单位来自已审阅的声明和源码证据；普通数值采样不能可靠反推出任意 C++ 的量纲。修改源码后旧单位证据须重新审阅。

系统在运行时会协商每个模型支持的维数、几何和字段。注册新的 `.cpp` 后，还须具备完整的参数声明及初始采样／AMR 能力，才能显示真实预览。二维柱坐标采用轴对称 `(r,z)`。其 Initial AMR 当前提供实际根级初始化快照；要求更高细化层级时显示受限状态，不能据此判断演化过程的动态 AMR。模型与后端的支持范围见[功能清单](../Features.zh-CN.md)。

## 查看 Plotfile

完整 `plt_XXXX.h5` 仍是权威数据，Studio 只读。总览支持笛卡尔一维和二维的已发布叶块，可用较低显示分辨率。正式发布文件支持按所记录的原生坐标查询笛卡尔、柱和球坐标的一至三维单元；例如显式 RZ 使用 `(r,z)`，无需把它转换成单元的笛卡尔中心。查询保留文件中的 native bounds、V/W、单位和身份；曲线/三维渲染与模拟的物理验收分别处理。Inspector 回查原文件中的 native cell。字段来自文件而非初始采样。

XDMF、可重建查询索引、有界跨查询缓存、更多几何/三维格式和完整全应用验收按[下一阶段计划](../development/ComputeStudioReleasePlan-20261006.zh-CN.md)闭环。交互检查图不代替科研分析或演化验证。

## 协作与验证入口

- [Core API 与兼容边界](../../src/api/README.md)
- [配置 v3 契约](../../src/api/docs/CONFIGURATION_API.md)
- [Studio/Host 工程测试](../../studio/tests/README.md)
- [科学验证](../../validation/README.zh-CN.md)
- [本轮集成与发布计划](../development/ComputeStudioReleasePlan-20261006.zh-CN.md)
- [历史交接与 UAT](../../studio/docs/archive/README.md)

环境安装、依赖检查和科学测试环境见[环境要求](StudioEnvironment.zh-CN.md)。
