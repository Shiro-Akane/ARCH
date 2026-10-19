# Plotfile Driver 发布失败与输出编号

## 范围与实际 finding

处于联合计划 3C 后的独立 plt 小切片，延续 Sod 1D / Cartesian 2D AMR。
基线 94a094f4f4c226de68cf64f52161877e890214be。
真实生产 DriverIO::write_plot 使用 ctrl.plt_file_index++ 作为 writer 参数：
HDF 写出或最终发布抛错时，writer 虽然传播异常，Driver 编号已经提前前进。

## 最小修复

仅将 Plotfile 编号递增移到 write_plt 成功返回之后。
输出时间累计和 calls 本来已在成功后，本次保持不变。
成功输出的文件名/编号序列、FP64 场值、物理算法、EOS、checkpoint 格式和正常身份语义不改。
不修改 checkpoint writer、checkpoint index 或其现有发布方式。

## 故障复现与验证

新增 tests/host/io/test_driver_plot_publication.cpp。
使用真实 DriverRuntime、CPU 单块 AMR、DriverIO、PlotIO 和 HDF writer。
只有固定有效 closure witnesses，测试 IO 机制，不作为 EOS 精度证据。
没有调用 simulation timestep，测试末尾明确要求 time=0/step=0。

未修复真实 CPU 对象测试 exit 1：
failed publication advanced Driver plot index。
首次 fixture 编译成员名错误已修正，不计作 production failure 证据。

修改后既有 arch_solver_dispatch 增量重建，只编译 DriverIO.cpp；
最终五类 failure：write/flush/close（仅测试链接器包装）、
rename（正式目标为现存目录）、create（父路径为现存普通文件）。
逐项要求异常到达 Driver 调用者、失败编号不前进；清除阻碍后用同编号重试，
成功只递增一次。既有输出保存、临时文件清理均检查。
最终 exit 0；没有 fallback、模拟输出或放宽门槛。

既有 CTest plotfile_publication / checkpoint_compatibility 两项通过，0.15s。
一次直接 WSL 正则命令由于 shell 引号未执行；之后以 Python structured argv 正常运行，
不把该命令错误计作套件失败或成功。

## 复现

先正常完成 CPU ARCH build（对象集合必须对应待审源码），再执行：

    python3 -B validation/io/run_driver_plot_publication.py \
      --build /path/to/build-cpu \
      --output-root /persistent/local/new-fixture-directory

runner 使用调用者信任的 compile_commands.json 和 Ninja link argv，
替换 main object 为测试入口；不执行 shell、不 configure、不自行 build。
借用全部现有生产对象/dispatch library，保留相同数值编译 flags。
这是 Linux 手动 scoped regression，不声称已纳入 CTest/CI。
源码、dispatch library、测试 ELF digest 见同名 Summary.json；
原始 fixture H5、编译/链接 logs、ELF 保持本机 studio/.local。

## 未关闭

真实磁盘耗尽、硬件断电、fsync 持久性仍未验证。
本次不证明全域 AMR 覆盖、独立科学精度或完整 build/EOS 身份。
相邻 checkpoint writer 直接写正式文件并在调用前推进 chk index，
其原子发布/失败编号需要单独 review；本补丁没有将 Plotfile 证据延伸到 checkpoint。
后续继续来源身份、只读 Viewer 负向交互与索引/缓存资源证据。
