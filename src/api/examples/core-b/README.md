# Core B 真实响应样例

本目录的 JSON 来自实际 CPU ARCH 程序；不是合成热图。每份 `.json` 对应同名 `.par` 的原始字节，`identity.configRevision` 为其 SHA-256。成功样例用 5×3 采样，便于人工检查非正方形数组；参考默认仍是 128×128。

从仓库根目录运行，以便解析参考配置的 EOS 相对路径：

```sh
build-studio-cpu/bin/ARCH --preview CellularDet --config-stdin \
  --samples-x1 5 --samples-x2 3 --request-id cellular-x1 \
  < src/api/examples/core-b/cellular-x1.par
```

更换同名输入时，也将 `--request-id` 改为表内名称。以下响应保留程序输出格式；用 JSON 查看器可展开。

| 名称 | x1 × x2 | 退出码 | 用途 |
|---|---|---|---|
| [cellular-x1](cellular-x1.json) | 5 × 3 | 0 | x1 分界，查看沿 x2 的区域内场值变化 |
| [cellular-x2](cellular-x2.json) | 5 × 3 | 0 | x2 分界，查看 VELY、数组方向和 Inspector |
| [missing-eos](missing-eos.json) | 5 × 3 | 5 | EOS 文件缺失；保留已读取区域/AMR 和已注册组分 |
| [unsupported-direction](unsupported-direction.json) | 5 × 3 | 4 | shock_dir=2，二维预览明确拒绝 |
| [sampling-limit](sampling-limit.json) | 257 × 3 | 2 | 分配前拒绝轴采样超限，保留配置摘要 |
| [response-limit](response-limit.json) | 256 × 256 | 7 | 数量合法、实际 JSON 超过 8 MiB；保留状态，场数据为 null |

[capabilities.json](capabilities.json) 是同一程序的能力响应。它新增逐模型 `modelCapabilities`，顶层旧字段继续表示 Sod，以保持旧客户端兼容。A 目录的能力文件记录 A 独立提交；集成 A+B 时使用本目录的最新能力示例。

`response-limit.par` 把整个显示域放入扰动区域，使用较多有效数字，验证实际序列化边界。它是接口资源限制测试输入；不作为推荐模拟配置。

复现结果中的浮点末位、日志、编译块尺寸或源文件摘要可能随平台/构建变化。坐标排列、状态含义和身份规则按 [接口 README](../../README.md) 验收；不要按整份 JSON 字符串相等判断兼容性。
