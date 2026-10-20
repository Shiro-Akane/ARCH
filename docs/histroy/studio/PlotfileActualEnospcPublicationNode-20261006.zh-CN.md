# Plotfile 真实 ENOSPC 发布（2026-10-06）

联合计划 writer→query→Viewer 发布完整性出口；基线
e0507778028fac3ade77a7373b4325cfeb3d5870。PASS（有限范围）。

## 实际路径与结果
CPU DriverIO→PlotIO→HDF→内核ENOSPC→异常传播→清理→同序号重试。
没有修改scientific Core/production writer/ARCH binary，没有timestep。
- root仅用于unshare --mount --propagation private。runner拒绝非私有namespace。
- tmpfs限定4MiB、nosuid/nodev/noexec；fixture拒绝非tmpfs或容量>8MiB。
- 先发布index17；owned reservation实际write到errno28，保留4,169,728 bytes；
  statvfs f_bavail=0。不是fake HDF返回码，也没有填满项目磁盘。
- index18实际HDF create/header write报errno28。Driver失败传播、序号仍18，
  没有Saved PLT、最终文件或partial残留；已发布17的SHA不变。
- 释放唯一owned reservation后，同一index18成功，next index19。
- time=0/step=0；两个原始H5复制到本机持久.local/raw，归项目用户，
  保留private权限且已逐字节可读。finally卸载；外部findmnt恢复ext4 /dev/sdd。
- CPU32f7b139.../CUDA1cbbd695...前后未变；未重建ARCH。
- 既有empty-grid/write/flush/close/rename/create/retry回归通过，
  architecture/Python syntax/diff check通过。无新增CI job或科学矩阵。

## 修正与失败保留
旧manual runner同时链接旧/新PlotIO，当前直接object布局duplicate symbol。
现按可信compile_commands output替换直接对象；仅archive布局prepend。
原失败保留，最终fixture build11s、peak RSS895576KiB、swap0、guard未停止。
root只读git曾dubious ownership拒绝，尚未mount；改为准确safe.directory
命令参数，global config不变。首个成功raw副本保留root private ownership，
项目用户回查受阻；runner现仅将owned raw副本归项目用户，重测通过。
所有失败目录/日志保留，不删除原始证据。

## 复现
仅Linux/WSL、已有可信CPU build tree；两个输出路径须新建于
唯一workspace/studio/.local/integration：

    python3 validation/io/run_driver_plot_publication.py --build build-cpu --output-root NEW_BUILD --compile-only
    sudo unshare --mount --propagation private -- python3 validation/io/run_plotfile_enospc_namespace.py --fixture ABS_NEW_BUILD/driver-plot-publication --output-root NEW_RUNTIME

raw在studio/.local/integration/plotfile-enospc-runtime-readable-20261006/raw。
处理证据validation/io/results/plotfile-enospc-20261006/summary.json含准确
source/test ELF/scripts/binary前后身份与H5 hashes。H5/全量日志不提交。

## 限制
只覆盖ENOSPC create/header路径，不签收大型dataset中途满盘、fsync、
断电/crash持久性或checkpoint发布。固定callback仅用于IO，不是EOS科学oracle。
原JENS CUDA授权、RZ force/axis/viscosity/external contract、冻结长包保持开放。
