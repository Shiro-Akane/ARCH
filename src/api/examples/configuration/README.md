# 标准配置与单位响应示例

这些 JSON 来自本次 CPU ARCH 程序的实际响应。请从仓库根目录运行，保持 EOS 相对路径基准一致。以下示例将可执行文件位置记作 `build-studio-core-ui/bin/ARCH`。

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
build-studio-core-ui/bin/ARCH --inspect-config Sod --config-stdin \
  < src/api/examples/configuration/sod.par
build-studio-core-ui/bin/ARCH --preview CellularDet --config-stdin \
  --samples-x1 5 --samples-x2 3 < src/api/examples/configuration/cellular.par
```

使用 `--request-id` 可以替换示例请求标识；configRevision 是输入原始字节的 SHA-256。配置检查不需要 EOS 表；Cellular 实际预览需要先准备 `EOS_toolkit/tables/helmholtz/helm_table.dat` 的 Git LFS 实体文件。

Sod 响应使用模型单位 `code_*`；Cellular 参考配置使用 CGS。invalid-integer.par 最后加入 `nblockx2=1.5`，用于演示带参数键的错误。本目录是最新扩展的响应示例，旧 core-a/core-b 目录保留当时的接口快照。
