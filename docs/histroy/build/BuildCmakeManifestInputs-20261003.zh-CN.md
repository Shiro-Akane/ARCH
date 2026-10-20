# Build Manifest 的 CMake 输入证据接入

2026-10-03，基线5e472868。实际Ninja RERUN_CMAKE依赖项目CMake、
外部SuiteSparse配置、系统CMake模块和生成compiler配置，不能用仓库白名单代替。
复用Configure已有CMake File API采集，Build在前后读取cmakeFiles输入，
保存hash/size/generated/external/cmake与跨构建稳定性，freshness再次复核。
变化的CMake-only输入标needs-build，缺失/旧证据标unknown。
新增持久证据验证拒绝坏hash、重复路径及错误source/build绑定。
此链仍dependenciesComplete=false，不开启完整coverage声明。

独立fixture覆盖不在固定tracked列表中的依赖变化及corrupt Manifest拒绝；
真实安装CMake File API覆验默认路径/含空格路径和新loader。
现有ARCH build tree只读取得的输入统计见Summary，没有重新configure或Build。
旧Manifest不被改写或补造为拥有新before/after证据；
下一次正常Build才产生新的完整字段。

首次完整259项回归258通过，新增CMake unknown提示遮住既有toolchain提示；
调整生产诊断优先级后259/259、typecheck/lint/diff-check通过。
保留失败日志，未改原测试断言、科学阈值或错误判据。
原配置/编译器/linker unknown均保持fail-closed。
generated输入、Ninja图/命令、glob新增与工具消费者等全coverage仍需继续核对。
本步不运行Preview/演化/CUDA，不push/tag/main merge，不上传原始数据。
