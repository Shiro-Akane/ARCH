# MPI 迁移与后续验证：2026-11-01

## 来源与目标

本轮参照“理论注入”对话确认新的 GPU-273312 地址为 ubuntu@150.65.181.188。
本机旧的同名 SSH 别名仍指向旧地址，因此本轮使用核实后的 IP。
源服务器为 cpu-989544 / gen-989544（150.65.181.189）。
所有源码、环境、实验与数据在服务器处理，迁移通过服务器直连完成；
本机只发送控制脚本和接收简短文本，没有中转项目文件。

迁入的 MPI 代码固定为 29972a167c9bc0bb77bdb07557f920f54bfad64f；
独立对照为未修改的 main@5dc6d7bad3fcf37c1aa261d66ec1e8dcf6c5e21d。
这是既有一维固定根网格 CPU 流体切片。原数学目录与 MemoryPool 实现保持不变。

## 迁移与旧文件保留

目标的 /home/ubuntu/projects/ARCH 主副本停在 2026-07-25，有 164 项修改/新增/删除状态。
已保存 HEAD、状态及对应现存文件的 SHA256，迁移后再次核对一致。
该目录和其他历史 ARCH 工作树没有被覆盖，也没有删除源服务器结果。

迁移到目标上原先不存在的独立目录：

- /home/ubuntu/projects/ARCH-mpi：完整 Git 对象和源码；未复制旧构建产物。
- /home/ubuntu/projects/ARCH-mpi-evidence：原 CPU 全部实验记录，包含早期失败尝试。
- /home/ubuntu/projects/ARCH-mpi-toolchain/env：原工具链，保持同一绝对前缀。
- /home/ubuntu/projects/ARCH-mpi-dependencies/highfive-src：原 HighFive 源码。
- /home/ubuntu/projects/ARCH-mpi-baseline：在目标上重建的独立干净基线。

迁移清单覆盖 37,790 个文件和符号链接，另核验 environment.sh；
普通文件检查长度和 SHA256，符号链接检查目标，Git fsck 完整检查通过。
临时 SSH 密钥只允许指定 MPI 路径的只读 rsync；完成后撤销源端授权并移除目标端临时私钥。
完整清单与凭证保存在 /home/ubuntu/preservation/arch-mpi-migration-20261101/。
旧记录中的 LFS 发布失败是历史状态，迁移成功不代表 GitHub 发布或额度恢复。

## 新实验与执行修订

新输出位于 /home/ubuntu/projects/ARCH-mpi-evidence-gpu273312-20261101，
原 CPU 结果目录未改动。协议为新目录的 protocol.json。
Debug 和 Release 均独立构建串行 main、MPI ON 候选和 MPI OFF 候选。
复用 GCC 14.4.0、Open MPI 5.0.11、CMake 3.31.8 和原 HDF5/HighFive。
每次构建最多 8 个编译任务；实验不使用 oversubscribe，不使用 GPU 计算。
扩展计划为 6 组流体条件、256 步、17 根块、18 个全局 pool slot、
1/2/4/8/16 进程，每 16 步输出，与独立 main 的原生字段逐位比较。

初版编排漏设 ARCH_RUNTIME_OUTPUT_DIRECTORY，项目默认把 MPI ON/OFF
写入同一源码根目录的 bin。构建后的程序位置检查在应用对照之前拦截。
r1 显式分开各构建的输出目录，复用已检查对象，重新链接和验证。
求解器、数学实现、输入及数值验收标准未修订。
初版 runner 和失败状态保留；复用的成功构建/CTest 日志记录恢复执行，
初次命令和耗时仍在 execution-status.json。
有效执行状态见 execution-status-r1.json 与 execution-revision-r1.json。
最终结果另附独立验证 JSON，不能由本执行计划推断完成情况。


## 最终核验结果

- Debug：26 组应用对照全部逐位一致，7 类无效配置按预期失败，5 项 MPI / 6 项串行 CTest 通过。
- Release：相同 26 组应用对照、7 类预期失败和 11 项 CTest 全部通过；MPI OFF 无动态 MPI 依赖。
- 跨服务器复现：新 Debug 与原 CPU 的 32 条运行记录逐位一致。
- 扩展：6 组条件 × 1/2/4/8/16 进程 = 30 组，均完成 256 步、17 次输出；
  17 个根块共 272 个单元，所有原生字段与独立 Release main 逐位一致。
  多进程本地 pool 容量之和均为全局 18，未生成部分串行 checkpoint。
- 合计：82 组新数值对照、14 项预期失败、22 项 CTest；未使用 oversubscribe。
- 最终再次确认 14 个实现/测试文件的原 SHA256 不变，数学、物理、AMR 传递/细化和存储目录没有改动；
  旧 ARCH 工作树 164 项状态和文件内容仍不变。

完整逐条件记录见同目录 MPI-Migration-GPU273312-20261101.validation.json。
执行快照位于 validation/mpi/gpu273312-20261101；它们绑定本轮固定路径，
重跑应另立空输出目录和执行版本，不覆盖已有结果。初版失败脚本和状态保留在服务器证据目录。
新证据的完整文件 SHA256 清单保存在迁移凭证目录的 new-evidence-manifest.json。

本轮仍为单节点 CPU 一维固定网格验收；没有宣称 MPI 已支持 AMR、多 GPU、
跨节点、分布式重启或 Windows。256 步比较不是普遍长期稳定性证明，也不是加速比测量。
下一步可在这一已复现基线上分别设计二维/三维棱角交换与真正跨节点传输验收，
继续沿用现有 exchange、状态与内存池接口。

本轮运行入口为 build-gpu-release/bin/ARCH（MPI ON）和 build-gpu-release-serial/bin/ARCH（MPI OFF）；独立基线位于 ../ARCH-mpi-baseline/build-gpu-release/bin/ARCH。源码根目录 bin/ARCH 属于初次输出目录冲突留下的构建文件，不作为本轮验收入口。
