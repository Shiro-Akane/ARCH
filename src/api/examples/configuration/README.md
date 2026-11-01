# 标准配置与单位响应示例

这些 JSON 是配置 v2 的 CPU 响应样本，保留其默认值和语法。当前接口见[配置 v3](../../docs/CONFIGURATION_API.md)，实际 v3 样本见[configuration-v3](../configuration-v3/README.md)。下方命令记录样本的调用形式，路径基准为仓库根目录。

| 文件 | 请求 | 退出码 |
|---|---|---|
| schema.json | `--config-schema` | 0 |
| inspect-sod.json | `--inspect-config Sod --config-stdin`，输入 sod.par | 0 |
| inspect-cellular.json | `--inspect-config CellularDet --config-stdin`，输入 cellular.par | 0 |
| invalid-integer.json | `--inspect-config Sod --config-stdin`，输入 invalid-integer.par | 3 |
| preview-sod.json | `--preview Sod --config-stdin --samples 4`，输入 sod.par | 0 |
| preview-cellular.json | `--preview CellularDet --config-stdin --samples-x1 5 --samples-x2 3`，输入 cellular.par | 0 |

复现例：

```sh
bin/ARCH --inspect-config Sod --config-stdin \
  < src/api/examples/configuration/sod.par
bin/ARCH --preview CellularDet --config-stdin \
  --samples-x1 5 --samples-x2 3 < src/api/examples/configuration/cellular.par
```

使用 `--request-id` 可以替换示例请求标识；configRevision 是输入原始字节的 SHA-256。配置检查不需要 EOS 表；Cellular 实际预览需要先准备 `EOS_toolkit/tables/helmholtz/helm_table.dat` 的 Git LFS 实体文件。

Sod 与 Cellular 响应均使用 CGS。invalid-integer.par 加入 `nblockx2=1.5`，用于展示带参数键的错误。
