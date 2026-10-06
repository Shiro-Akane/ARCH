# Plotfile Linux production 原生错误保留验证

## 真实环境

Studio launch commit79e2c9c8e6dfc8415f15d7c81a405ebd73b9987e；production assets来自65aed733。
managed detached source64b0ce2f、CPU binary d53501ae...、Build92d429da。
完整依赖freshness仍unknown。原始Sod/Cellular文件及本机逐字节副本身份见Summary，
producer binary f82bb7ff...不能被当前运行Host使用的binary身份覆盖。
本轮没有Core Build、simulation、Save或科学原值变化。
启动Real Config期间观察到Init Preview preparing/current后才切换工作区，
因此不能宣称本轮完全没有Preview活动；本轮未点击Generate。

首次build-cpu binary没有匹配的Host-owned profile，真实窗口29100426明确拒绝，
正常关闭exit0；不注册临时绕过profile。改用既有独立受控CPU project/profile，
新窗口34147344成功；原输入/H5只复制到ignored .local，未修改任何source。

## 原生失败保留证据

实际窗口选择Real Plotfile，输入项目相对CellularDet.h5路径，Read metadata成功：
time0、Cartesian2D、20blocks×[16,16]、文件SHA8cc5e9e1...，
case/raw config/binary/EOS/session的文件证据保留；effective/build/sourceGit为unknown。
这些状态仅是recorded candidate，未改为科学认证或producer freshness。

实际Read global display LOD完成：5120leaf cells→32×24pixels，
x1[0,25.6]、x2[0,12.8]cm、DENS、20/20native leaf outlines、level1/2。
LOD为坐标重叠加权display mean，不是原生cell或科学积分。

随后将路径明确改为不存在的missing.h5并点Read metadata，
界面显示真实ENOENT错误，Observed file/SHA/shape仍为之前成功的CellularDet。
实际滚动回图形区，旧CellularDet路径/SHA、同一domain、32×24LOD及20/20轮廓仍显示。
失败未发布新metadata/图形；这是一项真实native failure-retention证据，
不覆盖所有损坏文件/身份变化/并发race类型。

## Cancel 可达性 finding 和证据边界

唯一Cancel read位于页面顶部，在source evidence、query controls或plot视口下不可见。
本次小文件扫描已经完成，未执行可证明的active cancellation。
不通过重复点击、人为延时或取消已完成任务把这一项标PASS。
下一步应使active read status/Cancel在查询与显示区可达，再做真实取消和旧请求淘汰验证。
当前native cancel/race仍未完成；不把既有Host自动检查等同于这两项native UAT。

正常close exec26921 exit0；已捕获8个owned PID/startTicks均退出，
输入和两份H5原件/副本SHA未变、配置指定output不存在、managed工作树clean。
只提交处理后摘要；日志/配置/H5留本机ignored .local。
没有源码改动，不重复331项已过baseline；全项目、科学CPU/Jeans/RZ/CUDA/O9仍未完成。
