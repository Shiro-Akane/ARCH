# Core A 交接

> 历史交接记录：本文保留当时的功能和测试范围。本轮新增能力与单位变更请以 [LOCAL_WORKFLOW_HANDOFF.md](LOCAL_WORKFLOW_HANDOFF.md) 为准，当前响应示例见 examples/local-workflow 与 examples/configuration。

## 分支与基线

- 实现分支：`codex/studio-core-a`，从远端 main `01cc4f723e674d47fe23850e7c0fef221e92e98c` 建立。
- 基础移植提交：`40b7704d4f583aa5b556f63bdbfe16815a6a7e82`，仅接入此前 `4c0fd5c1242d9a48d2a75d7a2c546abb34f83627` 的 Core CPU 预览部分。
- 需求依据：`studio/phase2e-core-contracts` / `31f6f8dbc7e6841c9c90992699b2a89f4dd58603` 中的 Contract A。
- A 实现在基础移植之后的独立提交中交付。准确最终 SHA 随交接消息提供，避免文档嵌入自身提交号。

已有 Studio `43b381c3` 已含基础预览代码；接入时只需取 A 增量，不需要重复取基础移植提交，也不需要为这个接口合并整个 main。Studio 前端与 Host 未包含在本分支的实现范围。

A 增量已对交接提交 `31f6f8db` 的对应文件执行补丁应用检查，通过且无冲突。Studio 应在接入该增量后按本机配置重新编译，再进行 Host/UI 验收。

## 已提供

- 继续调用 `ARCH --preview Sod --config-stdin`；schemaVersion 仍为 1.0。
- 新增可选的 parameterMetadata v1 与 graphicalBindings v1，并由 `--preview-capabilities` 的 extensions 明确列出覆盖范围。
- 首批只覆盖 Sod x_pos。实际 Get 调用提供默认值、实际值及来源；Sod 提供内部实际位置和当前域约束。
- 显式、缺失、解析失败回退、重复 key、数值前缀、越界失败和未知来源的表达见 [README](README.md)。
- Setup 之前的失败不编造 metadata；Setup 已读取后发生的失败保留已确认的信息，成功绑定仅随完整成功预览发布。
- EOS/grid/AMR/species 快照继续提供；没有时间推进、科学输出、临时 `.par` 或 CUDA 初始化。

完整成功、默认、回退、越界、拒绝响应及对应输入见 [examples/core-a](examples/core-a/README.md)。原有一维场和错误响应示例也已由当前程序重新生成。

## 构建与针对性验证

2026-09-19，在 Linux CPU Debug 构建验证：C++20，CUDA OFF，KLU OFF，OpenMP ON，BUILD_TESTING ON。本地构建复用已存在的 HighFive 源码目录；独立构建使用仓库默认的 HighFive 获取方式即可。

```sh
cmake -S . -B build-studio-cpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS_DEBUG='-O0 -g1' \
  -DARCH_ENABLE_CUDA=OFF -DARCH_ENABLE_KLU=OFF -DBUILD_TESTING=ON \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-studio-cpu/bin"
cmake --build build-studio-cpu \
  --target ARCH arch_preview_initial_conversion arch_preview_parameter_reads -j 2
ctest --test-dir build-studio-cpu -R '^preview_' --output-on-failure
```

| 测试 | 结果与范围 |
|---|---|
| preview_initial_conversion | 通过；共享初始状态转换与内存配置解析 |
| preview_api_contract | 10 项通过，1 项主动跳过；原一维字段、真实 Helmholtz 加载、状态快照、配置摘要、取消和无文件输出 |
| preview_parameter_reads | 通过；重复读取歧义、程序改值来源、严格开区间、普通配置不启用读取记录 |
| preview_parameter_metadata | 7 项通过；真实 CLI 的来源、默认值、位置与密度场一致、当前域、越界、重复 key、前缀及错误状态 |

四组 CTest 全部通过。正式 simulation/Plotfile 对照须显式设置 `ARCH_PREVIEW_SIMULATION_ORACLE=1`，本次没有启用；没有运行完整 baseline、CUDA 或 Studio UAT。

## Studio 接入事项

- 按 README 更新扩展类型、能力协商与响应校验；不要从布尔 markers 推断任意模型都存在可编辑绑定。
- 更新编译输入跟踪，覆盖新增的 `src/api/configuration/ParameterMetadata.h/.cpp`、`src/interface/PreviewMetadata.h`，以及此次修改的 GlobalDefs、RuntimeParams、ProblemGenerator、GenericProblem、Preview.cpp 和 Sod.cpp。
- 拖动只形成一次工作副本编辑/撤销，标记 stale；用户点击 Update Preview 才请求计算，不自动保存。
- 缺失 x_pos 的安全插入由 Studio 实现并独立验收。
- 图、绑定、metadata、Inspector 和状态快照按相同请求身份关联。失败保留旧图时，不混入失败请求或迟到请求的状态。
- 显示区域、EOS 加载状态、组分和 AMR 配置；当前接口没有实际 AMR 层级。

CellularDet 二维 Preview 属于 B，本次没有实现；A 可直接开始独立接入和验收。
