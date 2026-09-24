# Core A 完整响应示例

本目录 JSON 均由实际 CPU ARCH 可执行程序生成。每个响应旁边的同名 `.par` 是完整输入字节，响应 `configRevision` 为该文件的 SHA-256。采样数量为 4，requestId 为 `core-a-<名称>`。

| 输入 / 响应 | 含义 | 退出码 |
|---|---|---|
| [explicit.par](explicit.par) / [explicit.json](explicit.json) | 显式 x_pos=0.35，成功并提供绑定 | 0 |
| [default.par](default.par) / [default.json](default.json) | 缺失 x_pos，实际采用 0.5 | 0 |
| [fallback.par](fallback.par) / [fallback.json](fallback.json) | x_pos=bad，原有读取过程回退 0.5，并提供参数级诊断 | 0 |
| [outside.par](outside.par) / [outside.json](outside.json) | 域为 (-3,5)，x_pos=5 被 Setup 拒绝；保留实际值和约束 | 5 |
| [nonfinite.par](nonfinite.par) / [nonfinite.json](nonfinite.json) | nan 在 Setup 前被拒绝，没有虚构参数读取记录 | 3 |
| [capabilities.json](capabilities.json) | 当前 CPU 程序实际发布的能力和扩展版本 | 0 |

从仓库根目录复现一个成功响应：

```sh
build-studio-cpu/bin/ARCH --preview Sod --config-stdin \
  --samples 4 --request-id core-a-explicit < src/api/examples/core-a/explicit.par
sha256sum src/api/examples/core-a/explicit.par
```

替换文件名和 requestId 后可复现其他样例。失败样例预期返回上表中的非零退出码，stdout 仍是完整 JSON。相邻目录的 [missing-eos.json](../missing-eos.json) 展示 Setup 成功、EOS 失败时的 metadata 保留和空绑定列表。

数值前缀沿用现有解析规则，例如 `x_pos=0.35suffix` 会作为显式 0.35 读取；对应 scoped test 覆盖该行为。示例中的参数级 warning 不等于整个请求失败，调用方仍检查顶层 status 和退出码。
