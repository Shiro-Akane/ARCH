# Core B 交接：CellularDet 二维初始预览

## 分支与接入顺序

- 实现分支：`codex/studio-core-a`，继续使用 Core A 从 main 建立的分支。
- Core A 独立提交：`47517d1ce0ab33b761fcf3dd1a4241d68b7041fd`。
- B 是 A 之后的独立增量；准确 SHA 随交接消息提供。已有 A 的接收方只需取 B。
- 需求依据：`studio/phase2e-core-contracts` / `31f6f8dbc7e6841c9c90992699b2a89f4dd58603` 的 Contract B。

CellularDet 的二维显示本身不依赖参数 metadata 或 marker。本提交与 A 共用预览代码，移植时先取 A，再取 B；不需要合并整个 main。Studio 可以按原计划分别完成 A 和 B 的 UI 验收。

已对对方交接提交 `31f6f8db` 的对应文件快照检查先应用 A、再应用 B，两份补丁均无冲突。编译和下表测试在本 main 派生分支完成；Studio 合入后仍按自己的构建配置重新编译并验收。

## 已交付接口

调用同一个 ARCH 程序：

```sh
build-studio-cpu/bin/ARCH --preview CellularDet --config-stdin \
  --samples-x1 128 --samples-x2 128 --request-id cellular-001 \
  < simulation/Cellular/CellularPreview2D.par
```

- 从 `modelCapabilities` 读取逐模型能力；顶层旧能力字段保留 Sod 视图。
- 支持 Cartesian x1–x2、`shock_dir=0/1`；非活动坐标为 x3=0，随响应返回。
- 默认 128×128，每轴 2–256，总数最多 65,536。两个轴参数同时提供或同时省略，不能混用 `--samples`。
- `data.dimension=2`、`kind=grid`；`axes=[x1,x2]`、`shape=[Ny,Nx]`，按 `j*Nx+i` 取值。
- 字段为 DENS、PRES、TEMP、VELX、ENER、EINT、VELY；每项含数组、最小值、最大值，未知单位为 null。
- 直接调用现有 Cellular Setup/Init、共享初始能量转换和实际 EOS。没有复制 Cellular 的分界或噪声公式。
- 返回 EOS、基础区域、AMR 配置和已注册组分；仍是 CPU 初始采样。
- 输入最多 1 MiB；输出含换行最多 8 MiB。超限返回退出码 7 / `RESPONSE_TOO_LARGE`，保留身份和可容纳的状态，`data=null`。

精确字段、错误码及状态含义见 [README](README.md)。完整真实输入/输出见 [示例](examples/core-b/README.md)。

## 参考配置与依赖

使用 [CellularPreview2D.par](../../simulation/Cellular/CellularPreview2D.par)。参考输入选择 Helmholtz 和 aprox19，包含两个活动方向、分界位置及小幅场扰动。

从仓库根目录运行时，表路径为 `EOS_toolkit/tables/helmholtz/helm_table.dat`。其他工作目录使用有效的绝对路径或相对于运行目录的路径。不要直接把这一配置的 EOS 改成 ideal：现有核素组分没有 ideal 所需的热参数，预览会报告无效初值。

`radiusPerturb` 是所选轴的分界位置；`noiseAmplitude` 改变区域内的场值。此次不提供 Cellular marker 或拖动绑定，热图直接显示返回的数据即可。

## 构建和针对性验证

Linux CPU Debug，C++20，CUDA OFF、KLU OFF、OpenMP ON、BUILD_TESTING ON。复用 A 的构建目录也可。

```sh
cmake -S . -B build-studio-cpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS_DEBUG='-O0 -g1' \
  -DARCH_ENABLE_CUDA=OFF -DARCH_ENABLE_KLU=OFF -DBUILD_TESTING=ON \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-studio-cpu/bin"
cmake --build build-studio-cpu --target ARCH arch_preview_initial_conversion \
  arch_preview_parameter_reads arch_preview_sampling_limits arch_preview_cellular_reference -j 2
ctest --test-dir build-studio-cpu -R '^preview_' --output-on-failure
```

2026-09-19 最终回归：六组 CTest 全部通过，总耗时 385.09 秒。

| 测试 | 结果与范围 |
|---|---|
| preview_initial_conversion | 通过；共享初始状态转换和内存配置解析 |
| preview_api_contract | 10 项通过、1 项主动跳过；原一维契约与真实 EOS 路径 |
| preview_parameter_reads | 通过；Core A 读取来源及约束规则 |
| preview_parameter_metadata | 7 项通过；Core A CLI、字段与位置绑定 |
| preview_sampling_limits | 通过；数量/乘积检查、固定坐标、JSON 字节边界和状态保留 |
| preview_cellular_2d | 10 项通过；373.82 秒，详见下文 |

二维测试覆盖：5×3 非正方形采样、两个 shock_dir 的直接 Init/EOS 对照、噪声的区域内作用、默认与最大分辨率、输入/方向/配置/EOS/采样错误、真实字节超限、未保存文本摘要、等待输入及提交完整输入后的进程取消、CPU 与 CUDA 请求分离，以及无文件输出。

正式 simulation/Plotfile 对照保持主动跳过；未启用 `ARCH_PREVIEW_SIMULATION_ORACLE`。本次未运行完整模拟、CUDA 或 Studio UI 验收。

交付资料验证：

- CPU ARCH 及全部预览测试目标编译成功。
- 已生成 2 份真实 Helmholtz 成功响应及 4 份错误响应，逐份核对输入 SHA-256。
- 真实 256×256 超限请求返回退出码 7，错误响应保留请求身份、EOS、19 个组分及网格/AMR 状态。

## Studio 接下来需要完成

1. 能力协商识别 `modelCapabilities`，启用 CellularDet 二维请求并校验数量、字节和字段限制。
2. 为 grid 响应增加热图、字段选择、色标与 Inspector；横轴 x1，纵轴 x2 向上，保持数据索引不转置。
3. 将采样数纳入请求身份管理。请求仍提交完整的未保存参数文本；图、Inspector 和状态快照一起更新。
4. 参数或源码变化后标记旧图过期。取消、超时或失败保留编辑内容及旧图；新失败状态与旧成功图明确区分。
5. 编译输入跟踪覆盖新的 Sampling/Response 头文件以及 Preview、Json、Grid、ProblemHelper 的变化。新参考配置作为可选项目输入使用。
6. 使用示例中的两个分界方向和非正方形采样验收坐标、索引、色标及 Inspector，再检查取消、迟到请求与失败恢复。

Host 继续管理项目、编译产物身份、进程取消和超时；不需要新增后台服务或生产 helper。正式时间推进、CUDA、实际 AMR 布局、运行时输出显示和 Cellular 可编辑绑定留在各自后续任务中。
