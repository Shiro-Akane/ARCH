# 实际 Hydro repair 位置的 RZ 接线修复

## 复现与最小修改

基线64bb8390。DriverStages.advance_hydro的真实acceptance callback
使用Grid::GetPhysicalCoords未传Runtime profile，RZ repair事件被标成旧polar。

实际Host RK2第一次接受阶段产生非零floor事件：
代表单元独立原生坐标(1.03125,0,-0.9375)，旧记录
(0.61029898368911739,-0.83127114289383963,0)。
旧fixture exit1，失败在Hydro repair position used old polar chart；
准确旧ELF/头文件SHA及日志摘要见Summary。

修复仅给既有坐标转换传runtime.geometry_semantics()，
不修改repair算法、RK权重、EOS、场数组、角动量定义或预算。
Existing profile仍走原Grid转换，没有额外科学默认。

## 实际验证范围

复用RZCheckpointContinuation夹具，新--repair-position仅用于诊断测试。
min_eint=100刻意令原恒压轴向平移工程场触发floor；不是科学场景迁移、
不是为科学通过而调floor，也不变更独立验收误差阈值。
保持既有.001两次工程更新，不称为批准的物理终点。

四个mixed五叶块案例：r/z粗细接口×轴线/非零内边界。
每例第一步2560个RK-stage事件，代表位置分别：
(1.03125,0,-.9375)、(2.03125,0,-.9375)、
(.0625,0,.03125)、(1.0625,0,.03125)。
独立参考从原生block bounds、dx及ledger cell index计算，不调用待测坐标函数。
actual与expected精确一致。

真实Driver/Runtime/IO重编后ELF
6ffe45980d822bfc1582bffdf9917d16fafd61f19532144df58970347a022ae9。
非零事件路径4/4通过，write/read_chk后重建Runtime继续实际RK2；
原始科学状态bits、repair ledger values、position、代表block/stage/time一致，
来源checkpoint SHA不变。没有手工捏造RepairBudget事件。

同一ELF零事件模式4/4通过；4个分割checkpoint各21个dataset与修复前参考一致，
数值原bytes相同，字符串和所有旧metadata一致。
保留signed zero，没有放宽数值比较。未重复编译无变化代码。

## 状态与边界

关闭上一轮记录的Hydro representative-position finding。
主ARCH尚未为该header修改重编；新fixture ELF不冒充当前主binary。
没有新增非零legacy-chart事件UAT、公共RZ、真实regrid角动量或环体gravity科学验收，
CUDA与长期轨迹仍待。历史报告保留各自当时状态。
原H5/log/ELF本机ignored，仅源码、工具、处理后证据提交，无push/tag。
