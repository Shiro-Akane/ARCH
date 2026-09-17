# 旧 CUDA 主线构建目录退役（2026-09-18）

范围仅为服务器 `/home/ubuntu/projects/ARCH-cuda-v2-mainline-build`。
用户要求：迁出依赖和独有材料，保留并补传实验记录/日志，再清理旧目录。
本次不是新的物理优化或数值验收，也不改变朋友的 `main`。

## 依赖和现用项目

- HighFive 固定版本 `0d0259e823a0e8aee2f036ba738c703ac4a0721c` 已复制到
  `/home/ubuntu/projects/ARCH-dependencies/highfive-0d0259e823a0e8aee2f036ba738c703ac4a0721c`。
  760 个文件/链接逐项一致，G++ 11 头文件编译检查通过；不升级或替换库。
- 清理后保留原 HighFive 路径的兼容符号链接及少量父目录，避免改写历史记录、
  改动当前 CMake 缓存或触发 CUDA 重编译。
- 当前 `ARCH-native-wave-v4-20260916`、主线合并验证目录、工具链、cuDSS/cuBLAS、
  SuiteSparse、基线可执行文件、S4 原始证据均不在删除范围。
- 其他引用属于历史验证证据和旧重放脚本。重放旧实验须先恢复材料并重新构建；
  不保证已退役二进制路径继续可执行。

## 保存范围与边界

已发布的旧归档继续使用，不重复上传：

- 公共记录：`validation/backend/results/legacy-archive-20260917/shared/`。
- 个人记录（仅个人 main）：`archives/legacy-server-20260917/personal/`。
- 构建配置/命令：`validation/backend/results/server-storage-audit-20260917/rebuild-metadata.tar.gz`。

本次补充包仅进入 Arsenic-er 的个人仓库：
`archives/mainline-retirement-20260918/supplement/`。
历史源码标为未验收材料，不注册、不合入生产源代码；记录不会进入朋友分支。

补充包包含 170,681 个文件记录，65 个分卷，新增压缩数据 207,664,638 字节。
其中包含 1,513 个此前未发布的 Git 对象（59 commits、545 trees、909 blobs），
并核对两个 Git LFS 缓存路径的实际内容，不仅保存 LFS 指针。
8 个原 Git bundle 的所有公布引用已存在已发布 Git 历史，保留引用和校验记录。

压缩包按成员内容、路径、模式、时间及链接信息保存；相同容器和文件去重。
原压缩封装不保证逐字节重建，但文件内容使用 SHA-256 校验。
可重建的 ELF/对象文件/依赖缓存不保留；已确认失败的 `taskg-red-source` 源码副本排除，
失败实验日志仍保留。不将其他名称带 red/mutation 的测试误判为失败废弃源码。
凭据模式和敏感名称检查未产生待隔离条目；此扫描不等同于全面安全认证。

## 删除门槛

1. 全量重建补充包的文件内容，并验证先前归档/Git/blob/构建元数据引用。
2. 归档 push 到个人 main，本地 fetch 后再次核对分卷字节、SHA-256 和 Git 对象。
3. 检查旧目录文件清单、inode、mtime、硬链接、挂载边界、所有链接及活跃进程引用。
4. 核对原先记录的保护文件；只删除清单中的旧文件，空目录用 `rmdir` 删除。
5. 建立 HighFive 兼容链接，重新编译头文件检查并校验保护文件。
6. 保存和发布删除清单、释放空间及校验回执。

实施结果以 `validation/backend/results/mainline-retirement-20260918/retirement-receipt.json`
为准；该回执不存在时，不应把本计划文档当作“已删除”的证据。

## 恢复方法

`tools/mainline_retirement_restore.py` 按 `files.jsonl.gz` 中的精确 `origin` 恢复单个文件，
必须显式指定新的 `--destination`，拒绝覆盖现有文件，不跟随归档中的链接。
参数 `--package` 指补充包；`--shared` / `--personal` 指上述旧包；
`--metadata` 指旧构建元数据归档；`--git-repo` 指含已发布历史的完整个人 Git clone。

重复容器的成员从 `same_members_as` 指向的首个容器记录查找。
`git-objects/<oid>` 是原 Git 对象内容，`git_type` 标明类型；必要时可使用
`git hash-object -t <type> -w --stdin` 在独立恢复仓库导入，再依据保存的 refs 恢复历史。
这不会自动把历史候选分支合并到 main。
