# GNU 工具链组件身份：部分依赖覆盖

## 2026-10-03：GNU 工具链组件身份接线

基线a95f49465f7d6d17f571f9dacf496dd403ba7705，执行前工作树clean。
现有CMake File API确认C/CXX均GNU13.3.0，编译器分别/usr/bin/cc和/usr/bin/c++。
原driver指纹不能发现同driver调用的子程序被替换；增加Host-owned固定只读查询，
shell=false，Linux固定PATH/LC_ALL，5秒/1MiB响应预算。
GNU C/CXX记录cc1/cc1plus、collect2、as、ld、lto1、liblto_plugin.so的路径、
realpath、SHA-256、size及-dumpspecs SHA；外部specs存在时另记文件指纹。
不接受浏览器提供compiler/argv/env，也不使用browser或manifest任意路径执行。
组件文件读取保留预算/稳定性检查；缺失或畸形查询失败，不假定无影响。

成功Build以后Manifest可记录这些可选扩展；loadManifest拒绝畸形组件/重复role。
freshness重新从当前CMake工具链取得证据并比较；子程序、linker、plugin、
specs或组件集合变化触发needs-build。旧GNU Manifest缺少组件继续unknown，
不会追溯修改旧成功Build身份。本轮未重新Build ARCH，现有运行Host尚未重启
加载此Host变更，不声称现有a7c719a8 Manifest已经含组件。

定向16/16；完整Studio/Host233/233、lint、typecheck、production build和
diff check PASS。原bundle-size warning仍在。新增测试覆盖组件变化、builtin
specs变化、缺失/畸形响应、实际GNU工具链Manifest消费及旧证据unknown。
真实build-studio-cpu只读采集C/CXX各6组件成功，见
StudioGnuToolchainComponentEvidence.json；完整检查日志在本机ignored
studio/.local/integration/toolchain-component-regression.log。

dependenciesComplete仍false。现有ARCH.link.d含已删除的/tmp/*.ltrans.o，
不按文件名忽略它们；仍缺每次实际编译参数覆盖、implicit libraries及Build前后
完整工具链稳定性。工具链组件默认查询证据不是所有实际调用的完整证明。
没有修改Core/物理/冻结阈值、架构审计规则或历史G输入；
无新simulation/Preview/CUDA、无push/tag/main merge。3C原生生命周期剩余
矩阵继续推进，未提前进入全模型/JENS/RZ。
