# 服务器 ARCH 存储审计（2026-09-17）

## 执行前状态与范围

用户明确授权审计并清理服务器 ARCH 的不再需要的内容，同时保全必要代码和实验记录。
初始系统盘约 298 GiB，已用 294 GiB，可用仅约 4.3 GiB（99%）。
本记录的预提交阶段仅完成盘点、校验和备份；最终执行结果以 `cleanup-receipt.json` 为准。

本轮严格限制为逐文件清理，不递归删除目录、不更改源码、不停止进程、不清理其他项目。
ComfyUI、Wine/Tdx 和其他服务不在清理范围。

| 类别 | 数量／逻辑字节 | 处理及依据 |
|---|---:|---|
| 两个旧 CUDA 构建根中的 CMake 对象、PCH、ARCH 静态库 | 3,112 文件／14,969,388,510 B | 仅选 9 月 10 日以前的中间产物；要求原 CMake 配置和源码仍在，无符号链接、硬链接、Git 跟踪或活跃引用 |
| S4 重复 gzip 包 | 2,968,560,281 B | 与保留 Zstandard 包解码后完整 tar 字节、长度、SHA-256 相同 |
| S4 下载分片 | 10 文件／1,236,911,980 B | 拼接 SHA-256 等于完整 Zstandard 包；服务器和本机完整包均重新核验 |
| 源码、Git 历史、可执行文件、动态库、原始 HDF、日志、其余归档 | 保留 | 不进入删除清单 |

两个旧构建根：

- `/home/ubuntu/projects/ARCH-cuda-v2-mainline-build`
- `/home/ubuntu/projects/ARCH-cuda-v2-build`

`deletion-allowlist.json` 是最终候选路径白名单，不通过通配符执行删除。
中间产物是按可重建类别丢弃，**不是逐字节备份后再删**；老版本重新编译会产生重编成本。
旧可执行文件和所有第三方依赖保留，现有可执行文件的运行不依赖这些 `.o`／`.a`。

## 备份、保留检查与恢复

- `rebuild-metadata.tar.gz` 保存 482 个构建根的 47,118 项配置、生成命令与日志材料，原件也保留。
  本机逐成员长度和 SHA-256 校验通过，见 `local-backup-receipt.json`。
- `arch-files-before.jsonl.gz` 是 ARCH 文件元数据盘点；`build-roots.json` 对应旧构建目录及源码目录。
  编译中间产物需从保留的源码和配置重新构建，归档并不保证跨环境重建二进制逐位相同。
- `protected-hashes.json` 对窗口续跑目录、共享 HighFive 及关键工具、库、基准程序共 23,286 个文件作哈希保留检查。
- 当前 150/200 网络窗口测试根 `ARCH-native-wave-v4-20260916` 整体不清理。
  cuDSS/cuBLAS、验证 Python、现用工具链、SuiteSparse/HighFive 的依赖目录不清理。
- S4 保留的完整 raw 包：
  `/home/ubuntu/projects/ARCH-hpc-s4-validation-20260913/build/s4-validation-20260913-evidence.tar.zst`；
  本机副本：`C:/tmp/ARCH-perf-20260909/build/s4-validation-20260913-evidence.tar.zst`。
  SHA-256：`ffa2287b0e61c5b9f7090138add48e52b41bc454ba07db872103d1b421b0a493`。
  其他历史 raw 包仍按既有记录保留，不声称全部历史原始数据都进了普通 Git。
- 必要审计清单、构建元数据归档和回执进入双方固定协作分支，并合入个人 main；不更新朋友 main。

## 安全门与局限

脚本见 `tools/server_storage_{audit,prepare,verify_local,cleanup}.py`。
13 项 Linux 临时目录测试覆盖旧对象允许、新对象拒绝、内容尺寸变化、源码、依赖、硬链接、
父目录符号链接、缺失源码、越界路径、错误类别及身份服务的精确识别。测试不触及生产目录。

删除前重新核验白名单、文件身份及内容哈希，检查 Git 跟踪和可见的进程 cwd/exe/fd/maps；
每 100 个文件复查进程；逐项 unlink 并写入持久化日志。禁止递归清理。
服务器未提供免密 sudo；其他用户的 fd/maps 不一定可读，记录这一权限边界，并检查其身份及命令。
若发现相关进程无法核查、编译任务或任何活跃引用，脚本停止，不终止该进程。
已核实的不可转储身份服务 `(sd-pam)`／`sshd: ubuntu[@会话]` 不视为编译进程；
仍记录其不可见 PID，只有精确身份匹配且命令不含 ARCH 路径时才采用这一区分。

执行后对所有原 ARCH 文件（扣除白名单）复查存续和元数据，并再次验证保护文件哈希。
本轮不做新的物理／GPU 性能实验，不改变任何既有性能验收结论。
