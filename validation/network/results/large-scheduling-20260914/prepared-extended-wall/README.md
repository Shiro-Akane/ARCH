# 原 BE 大容量长轨迹：延长 wall 护栏的准备检查点

**本目录保存的是待运行配方，不是新数值或性能通过。** 原 1800 秒超时结果继续保留。
只有六项耦合正式矩阵全部结束、11 模块 1188 次运行／1155 次比较及双份归档完整性检查通过，
才允许启动本补测，不与正式计时并发。

固定 audit150／audit200、BE_NR、8／32-lane 池、每次 32→33 非均匀单元存储、
每段 16 宏步至 1e-9、rho=1e7、T=3e9、cv=1e8、rtol=1e-7。
原字段／limiter／组分门槛、Host KLU、CUDA factor-cache v2 和冻结 device factory 不变。
这是 focused IdealGas 轨迹，不替代完整 Helm 应用、正式性能样本或独立反应能量参考。

唯一协议调整是单次 harness 的 wall timeout 从 1800 延长至 21600 秒；
编译／链接命令仍为 1800 秒，四个 harness 的总外层护栏为 26 小时。
不增加物理时间、不降低精度、不减少核素、不启用观察器或 CPU fallback。
四个完整 harness 应覆盖八段存储轨迹；任何超时／错误仍记录为失败。

新 Python 工具和测试会隔离放入服务器 build 下的独立配方目录，
不覆盖先前冻结源码树中的工具。只重编容量可选的 Host 测试 harness；
运行前保存实际 factory、provider、可执行文件及配方 SHA。
服务器 worker 与 SSH 连接分离，所有原始日志与终态均归档并双端校验。
归档检查通过与数值检查通过分开记录。

本机工具测试五项已通过，包括模拟超时，验证 build/runtime wall 上限分离以及失败前产物指纹留存。
首次新增测试使用了 Windows 路径分隔符，模拟 native link 匹配失败；修正测试 fixture 为 POSIX 路径后通过，
两次日志均保留。该失败没有调用真实编译器、GPU 或物理代码。
另有相关 microphysics timing 13 项和 large application 8 项工具回归通过。
PowerShell／Bash／Python 配方已完成本机语法检查；真实服务器执行状态尚待产生。

完整应用已确认的 150／200 网络性能差距仍为 GPU 耗时约 CPU8 的 5.0–10.3 倍；
不能用这份补测准备记录声明大网络已经提速或已达到 CPU 水平。
