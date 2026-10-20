# Driver diagnostics：主 CPU CLI 验证

clean source b82205e2c6414f2d8783063d6ff086c06f66404c，
现有 build-cpu CPU Release，target ARCH，parallel8；3步 DriverIO/archive/link成功。
ELF SHA-256 7d0360de4ac9a3429d2a8ffec3908ca4424f2c7716096f2c44ad189662a9d8c4，
size7409736。已纳入统一diagnostic flush/close修复。

真实CLI既有Sod显式tmax0场景：
run_timings.tsv→/dev/full，exit1/cannot flush run timings；
cpu_stage_timings.tsv→/dev/full，exit1/cannot flush CPU stage timings。
移除故障的新目录恢复运行exit0；
9个Plotfile场完整数组和21个checkpoint dataset（20numeric）与前次参考一致，数值逐位不变。

原始冻结input SHA f90a1050996fc3d62f4beb6ceacf41005025c440bd14d75cf1ca863033f87406，
每次只改out_dir；未改物理参数、科学定义或budget。
不是simulation演化/CUDA、regrid与设备诊断真实fault、fsync/ENOSPC/断电验收。

构建成功后控制脚本引用input.par，而实际文件为Sod.par，保护性检查停止，
未启动CLI、未重复build。校验实际文件SHA后继续故障与恢复检查。
构建耗时未持久记录，summary为null并给原因，不倒推耗时或为补数字重新构建。
原ELF e75300fc… 已由新ELF替换；不把新报告commit当构建source身份。

原始H5/plt/checkpoint/ELF/log留本机ignored
studio/.local/integration/driver-diagnostic-main-cpu-20261004。
公开RZ、科学gate、统一CUDA和O9仍未完成；不push/tag。
