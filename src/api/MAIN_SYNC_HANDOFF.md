# Studio Core API：2026-09-24 主线同步

本次以 `origin/main` 的 `48f6d357` 为基线检查。该提交已经包含此前 UI contract 的 Core 实现（至 `d98f6f6e`）；主线后续完成了源码目录调整、参数退役和 self gravity 等更新。现有 GUI 命令入口、JSON 外层契约、Sod/CellularDet 初始预览及持续会话在本轮 CPU 验证中仍可使用。本同步只修正文档中的旧数量，不改 API 或物理代码。

## 接入方需要更新的认识

- 从当前构建的 `ARCH --config-schema` 读取目录。此基线返回 `configuration-schema` version `2`、92 个标准键；不要固定为早期交接文档中的 90 或 88。五个已退役键会返回 `RETIRED_PARAMETER`；`gravity_boundary`、`gravity_rtol`、`gravity_atol`、`gravity_max_cycles` 已加入 Gravity 组。参数说明和适用范围见 [CONFIGURATION_API.md](CONFIGURATION_API.md)。
- `ARCH --list-cases` 当前返回 14 个已注册模型；每个模型可查询初始化检查及源码对应的单位证据。完整初始场图与真实初始 AMR 网格仍仅对 Sod 1D、CellularDet 2D 开放。界面应读取每个模型的 `initialFieldPreview` / `initialAmrPreview`，并用 `ARCH --preview-capabilities` 的 `modelCapabilities` 确定维度和采样范围。
- `gravity_type=self` 的生产计算能力已有扩展，但 `JENS` 加密指标仍不可用；初始场与 AMR 预览不计算重力势或加速度图。不能把新增的物理求解功能当作新增的 GUI 场图能力。
- `--preview-session` 的逐行 JSON 命令及 `preview-session-result.response` 仍沿用单次命令的响应结构。Host 继续负责进程超时、取消、当前 binary 与项目身份、旧结果淘汰；Studio 按返回的身份同时更新场图、Inspector 与网格。
- 历史 handoff 中的基线、模型数与耗时是当时的记录。本文件、[README.md](README.md) 及运行中 binary 的能力响应描述当前主线；具体字段语义按各接口文档读取。

## 本轮核对

在独立主线工作区配置 CPU Debug 构建，`ARCH_ENABLE_CUDA=OFF`、`ARCH_ENABLE_KLU=OFF`、`ARCH_ENABLE_OPENMP=ON`。编译了 `ARCH` 与 API 测试辅助程序。以下 13 项定向 CTest 全部通过（417.69 秒）：配置检查、参数读取/metadata、旧预览、CellularDet 2D 真实 Init/EOS 对照、全部 14 个模型的初始化检查、实际初始 AMR 对照、持续会话、资源与精确采样缓存。

```sh
ctest --test-dir build-ui-main-check -R '^(preview_|configuration_api_contract|ui_expansion_contract|initialization_probe|case_inspection_contract)' --output-on-failure -j 1
```

实际查询同时确认：标准键 92 项，模型注册 14 项，完整场图和 AMR 网格各支持 Sod、CellularDet；会话、模型检查、配置与 AMR 扩展均出现在 `--preview-capabilities.extensions`。本轮未运行 Studio 界面 UAT，也未编译 CUDA。

## 文档入口

1. [CONFIGURATION_API.md](CONFIGURATION_API.md)：标准键、退役键、单位、适用条件与可编辑选项。
2. [CASE_INSPECTION_API.md](CASE_INSPECTION_API.md)：14 个模型的 Setup/Init 检查及单位证据边界。
3. [INITIAL_AMR_API.md](INITIAL_AMR_API.md)：初始 AMR 网格与资源估算。
4. [PREVIEW_SESSION_API.md](PREVIEW_SESSION_API.md)：持续编辑的 NDJSON 会话协议。
5. [PREVIEW_SESSION_HANDOFF.md](PREVIEW_SESSION_HANDOFF.md)：既有 Host/Studio 分工；其中的旧基线与测速为历史记录。

跟随 UI contract 分支时需要重新构建 CPU binary，再让 Host 查询本次 binary 的 schema、case 列表和预览能力。单独更新前端静态列表不足以获得主线新增参数与模型。
