# 旧工作副本归档与失败源码退役（2026-09-17）

本批接续 `server-storage-audit-20260917`，只处理历史材料，不改当前物理实现、
restart、数值容差或 GPU 续跑目录。用户补充要求：旧数据、日志和源码可以归档，
但失败实验的源码不再补传，确认无用后直接删除。

本机完整性验收已完成（北京时间 2026-09-18）：公共包 49,642 个路径、16,909 个分块、
47 个包全部通过；个人包 17,566 个路径、508 个分块、1 个包全部通过。
两部分合计 67,208 个可重建路径，完整文件 SHA-256 和已有 Git blob 均已核对。

## 已执行的源码删除

仅处理服务器这一份已被后续成功版本替代的失败快照：

`/home/ubuntu/projects/ARCH-cuda-v2-mainline-build/task-stage1-20260827T/taskg-red-source`

- 删除 199 个 C/C++/CUDA 源文件及根构建配置，共 2,659,309 B。
- 删除前逐文件核对路径、inode、mtime、SHA-256，拒绝符号链接、硬链接及活跃进程引用。
- 删除后 1,130 个保留文件的哈希不变；数据、日志、脚本和文档保留。
- 没有为这些失败源文件新增源码备份或上传；清单只保存路径、大小和哈希。
- 不清除既有 Git 历史，不改写此前的归档容器，不终止任何服务或实验进程。

判定证据不是目录名：同级 `logs/taskg-formal-red.stderr` 记录 CMake 缺少测试源码而失败，
后续 `logs/taskgh-cpu-ctest.stdout` 记录 14/14 测试通过。两份日志的 SHA-256
均在 `failed-source-retirement/source-cleanup-receipt.json` 中。
`mutation` 等预期失败的负例测试不等于废弃实验，没有按名称批量删除。
进程检查使用普通 ubuntu 权限，没有 sudo；其他用户的 fd/maps 可见性有限，
沿用前一轮存储审计的保守分类，并保留 `process-check.json` 中不可访问 PID 的记录。

## 本批归档范围

服务器保留目录：`/home/ubuntu/projects/ARCH-legacy-archive-20260917`。
原始日志和数据仍在原处，本批没有因打包成功而继续删除原件。

| 发布范围 | 可重建历史路径数 | 已发布 Git 对象引用 | 新增日志/数据路径 | 逻辑内容字节 | 压缩数据包字节 |
|---|---:|---:|---:|---:|---:|
| 双方固定协作分支 `shared/` | 49,642 | 20,603 | 29,039 | 5,083,816,930 | 131,028,106 |
| 仅个人 main 的 `archives/legacy-server-20260917/personal/` | 17,566 | 5,381 | 12,185 | 196,168,570 | 911,894 |

逻辑大小包含多个旧目录中的重复内容，并不等于新增唯一数据量。合计 67,208 个路径，
5,279,985,500 B 逻辑内容；新增唯一内容先按 4 MiB 分块去重，再无损压缩。
源码如果已经存在于发布过的 Git 历史中，只记录原路径与 Git blob，恢复时读取该对象。
没有重新上传未验收的实验源码副本。

边界和待审项：

- 13,439 个未发布且验收状态不明的源码路径仅列入 `held-for-review.json`。
  它们没有被认定为失败，也没有删除或加入数据包。
- 1,135 个既有 tar/zip 容器及 9 个 Git bundle 仅登记于盘点清单，仍保留在服务器；
  这批没有解包或全部重新推送，不能称为“所有历史原始数据都已上传”。
- 编译产物、第三方依赖目录、私密配置和当前续跑根不在本批新增上传范围。
  历史日志中的构建命令、工具版本和过程采样属于原始证据，不代表当前环境配置。
- 强格式凭据扫描未发现匹配；这是一项有限检查，不是对任意形式秘密的绝对保证。
- 朋友的 main 不变；个人 main 独有的研究资料不进入朋友的固定分支。

## 校验与恢复

包内 `files.jsonl.gz` 保存原始相对路径、mode、mtime、长度、完整文件 SHA-256，
以及分块或 Git blob 引用；`chunks.json` 与 `volumes.json` 保存块和包的校验信息。
普通 Git 存储压缩包，每个包小于 40 MiB；不把原始大 HDF 逐个塞入 Git。

在包含 `published-git-bases.json` 所列历史对象的完整仓库中运行：

```bash
python tools/legacy_archive_pack.py verify validation/backend/results/legacy-archive-20260917/shared
python tools/test_legacy_archive_pack.py
```

`verify` 会逐包、逐块、逐文件检查长度和 SHA-256，并核验 Git blob；临时展开共享内容
约需额外 1.5 GB 空间。浅克隆缺少历史对象时需要先取回清单中的提交。
本机真实包校验回执为各包目录的 `local-verification.json`。

恢复单个文件时，从清单选择完整的 `path`，写入一个新的目的文件：

```bash
python tools/legacy_archive_pack.py restore validation/backend/results/legacy-archive-20260917/shared \
  --origin ARCH-cuda-v2-mainline-build/task-stage1-20260827T/logs/taskg-formal-red.stderr \
  --destination build/restored-taskg-formal-red.stderr
```

恢复工具拒绝覆盖既有文件，输出前核验完整内容哈希；不会自动写回服务器原目录。
本机另做了真实恢复抽查：失败日志、后续成功日志以及一个历史 HDF 输出均恢复成功，
SHA-256 与清单一致，见三个 `restore-*.json` 回执；两份原始日志附在删除审计目录中。
包的完整性验证不是新的数值回归或性能实验，不改变既有 GPU 验收结论。

## 发布方式

为避免通过较慢的远程连接来回搬运同一批数据，在服务器独立的
`ARCH-archive-publication-20260917` 稀疏克隆中组装公共提交。
`tools/legacy_archive_publish.py` 重新核对包哈希，并要求完整 Git index tree
与已核验的本机暂存树完全相同，才允许生成提交。该脚本本身不 push。
本机接收并核验同一个提交对象后，双方固定分支采用普通、非强制 push；
个人资料在独立的 main 提交中保留，再合并公共提交。不会覆盖朋友 main 或重写历史。
