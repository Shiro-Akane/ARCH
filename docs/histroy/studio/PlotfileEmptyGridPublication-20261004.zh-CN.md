# 空活动叶网格的 Plotfile 发布失败
## finding 与范围
基线 ab0a345868998422e179b398ced90baed78c6169。
直接 write_plt 在 num_blocks==0 时返回，且先创建输出目录。
Driver 在正常 writer 返回后递增 plt_file_index，因此不可把这种返回作为完成发布。
实测旧 cached writer 直接入口反例失败：direct writer accepted empty active grid。
首次 Driver-only 反例也失败，但未记录实际异常，不能据此声称已复现 Driver 序号递增。
保留所有初次失败；后来直接 writer 反例独立证实接受空网格问题。

## 最小修复
PlotIO 在创建目录/物化输出前拒绝空 active leaves，抛明确 invalid_argument。
DriverIO 在 materialize_current_for_host 前作同一拒绝，避免空拓扑走入资源路径。
未修改物理场数组、FP64、EOS、几何测度、checkpoint、科学阈值或 AMR 算法。
空网格不是可发布结果；不添加虚假空文件或成功标记。

## 实际验证
既有真实 CPU IO fixture 增加直接 writer 与 Driver 两个入口。
空网格必须明确报 active leaf 错误、不创建输出目录、保留17号。
随后真实1D fixture 仍成功发布17号并仅推进一次。
既有 write/flush/close 注入和真实 rename/create 冲突继续验证：
向 caller 传播、序号不推进、不日志成功、不改变旧正式文件、同编号重试，
无 partial 泄漏；所有步骤 time=0 / step=0。
最终 suite exit0。过程中五轮结果均保留，退出码依次1/1/0/1/0，
其中第四轮为新增 direct writer 反例对旧对象的明确 FAIL。

runner 从可信既有 CPU CMake compile_commands 分别编译当前 fixture/PlotIO/DriverIO，
显式链接这些 object，再复用其余原 CPU objects，不执行 configure/build ARCH/clean，
不修改 build tree。补充 source/object/library/fixture SHA 与 HEAD/dirty 状态。
测试使用 dirty patch，准确文件 SHA 见 Summary；不得冒称整个 binary 已包含当前 HEAD。
baseline 旧 runner 的 driverSourceSha 是读取磁盘文本，不是其 cached object 的生产身份；
故本报告以 library/executable SHA 区分旧证据，不从该字段伪造 freshness。

Python AST parse 与 git diff --check PASS。没有 Studio 变化，不重复334项 UI suite。
原始 fixture H5、可执行文件及完整日志保留 ignored .local；
提交仅代码、脚本与处理后报告。既有 science H5 未重跑或改写。
不是实际 ENOSPC、fsync/断电持久性、科学精度、完整 CPU/CUDA 或 O9 验收。
