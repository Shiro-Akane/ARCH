# 验证数据可用性

[English](README.md) · [验证索引](../README.zh-CN.md)

仓库按模块保存紧凑指标、参数文件、独立参考采样和复现脚本。完整程序输出和逐步
执行轨迹另行索引，使源码检出包含理解数值结果所需的数据，而不附带所有生成的
工作文件。

[raw-records-20261006.json](raw-records-20261006.json) 按模块、原始文件名和源码
提交索引 9,549 个归档文件，共 147,038,657 字节。这些文件原为普通 Git 对象，
可以从所记提交恢复：

```sh
git show <source_commit>:<repository_relative_path> > /tmp/recovered-record
```

产生数据的机器也在 `studio/.local/validation-archive/20261006/` 下按原仓库相对
路径保留文件。本地 `manifest.json` 记录路径、大小、Git blob 身份和保存检查；
这部分本地数据不包含在新克隆中。

结果 JSON 中的路径注明该次执行所用文件，因此可能指向本地文件，而非随仓库
分发的夹具。阅读指标时也应查看结果状态，例如保留的 audit31 memcheck 超时
属于未完成执行。

EOS 运行表仍位于 `EOS_toolkit/`，回归输入位于 `tests/fixtures` 和各模块的
`inputs/`。测试自行生成状态输出。新测量的大体积检查点、Plotfile 和完整轨迹
应附源码与输入身份索引；研究需要公开这些数据供独立分析时，可另行发布数据归档。
