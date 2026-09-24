# 本地工作流实际响应

本目录 JSON 由本轮 CPU Debug binary 实际产生。从仓库根目录调用，stdin 使用表内 `.par` 原始字节，requestId 未指定（空字符串）。EOS 表须为仓库对应 LFS 实体。响应中的 configRevision 对应该输入 SHA-256。

| JSON | 命令（前加 ARCH） | 输入 | 退出码 / 状态 |
|---|---|---|---|
| registered-cases.json | --list-cases | 无 | 0 / ok |
| capabilities.json | --preview-capabilities | 无 | 0 / ok |
| sod-resources.json | --amr-resources Sod --config-stdin | sod-amr.par | 0 / ok |
| sod-mesh.json | --preview-amr Sod --config-stdin | sod-amr.par | 0 / ok |
| sod-limited.json | --preview-amr Sod --config-stdin --mesh-max-blocks 6 | sod-amr.par | 0 / limited |
| cellular-mesh.json | --preview-amr CellularDet --config-stdin | cellular-amr.par | 0 / ok |

`limited` 不是完成，仍可带完整平衡的较粗网格。默认网格预算是 512 块 / 128 MiB，实际工作容量还受配置与每块存储预算约束。

更新的 90 参数目录和 CGS 场响应见相邻 [configuration](../configuration/README.md)。这些是响应示例，不是要求前端硬编码的模型或网格列表。

模型检查与绘图统计示例：

- `case-inspection-sod.json`：完整已观察参数、默认值/采用值、CGS 单位依据和真实 Init 样本。
- `case-inspection-cellular.json`：温度输入、组分输入读取与输出质量分数。
- `case-inspection-sedov3d.json`：3D 参数检查、27 个 Init 样本，explosion_energy 为 erg。
- `case-inspection-invalid-int.json`：custom 整数输入拒绝小数，保留部分读取结果。
- `capabilities.json` / `registered-cases.json`：模型检查、初始场和 AMR 能力分别查询；检查时间预算与编译源码身份。

这些 JSON 由本轮 CPU binary 实际生成；机器上的源码绝对路径仅用于说明构建来源。Host 应关联自己的本地构建清单。旧版本示例目录记录历史行为。
