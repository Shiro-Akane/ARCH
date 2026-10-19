# Sedov 二维 Linux 原生桌面验收

联合计划全模型初态/初始AMR矩阵增量；首次补齐Sedov原生用例。
Studio基线ff680898d320bc1f8ed7eaacf5d1f5cf0fff0f2e；独立clean受管源码
64b0ce2f8d97553f59024978618f1e4848a974e1。
CPU binary d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75，
Build92d429da-42d9-4336-b956-79c43440b5c3，完整依赖freshness仍unknown。
production React assets +正式Linux Electron/owned Host，非Vite/浏览器替代。

## 实际输入与字段

复用已有有效Sedov.par原字节，仅复制到独立本机UAT目录；
input SHA35646e15bfb001d53c6a9987abf596ef0e0bb683bb6bfd4070ef2fe90b899137。
这是显式finite deposition radius=.03的二维Cartesian教学输入，不是奇异点源。
没有修改物理、配置、阈值、EOS、checkpoint、Run/Restart或进入simulation。

窗口真实初始化128×128，shape[128,128]、x1-fastest、x3=0，
domain两轴[0,1]cm。Density1；原生菜单切Pressure显示局部沉积区域，
range1e-5..141.471erg/cm^3。菜单点击先仅高亮，Return后才观察到真实字段变化，
未将未提交的菜单高亮算作切换通过；随后点击外部关闭菜单并拖动滚动条。

点选沉积区sample8127=(63*128+63)，坐标(.49609375,.49609375)cm；
Inspector rho1、P141.47107erg/cm^3、T353.67768K、VELX/Y0。
点选环境区sample3302=(25*128+102)，坐标(.80078125,.19921875)cm；
rho1、P1e-5erg/cm^3、T2.5e-5K、VELX/Y0。
这些按实际UI精度记录，不能替代独立解析/EOS科学oracle。

## 真初始多层AMR

通过原生按钮Generate initial AMR，Preview预算512blocks/128MiB，
.par max_blocks2048保持。完成3轮，52leaf，L0/L1/L2/L3=12/12/12/16；
complete=true、no preview limit reached，working capacity230。
真实block outline/cell lines叠加在压力图，matching identity标签出现。
资源表显示full-domain refinement估计16/64/256/1024leaf，
明确不是局部细化预测或OOM保证。

启用Select AMR block on plot，点选到3:15:14:0：
level3、logical[15,14,0]、lower(.46875,.4375)cm、
upper(.5,.46875)cm、cellShape16×16、spacing两轴.001953125cm。
由原输入4×4roots及2^3细化，独立算术
lower=logical/(4*2^3)，upper=(logical+1)/(4*2^3)，
spacing=1/(4*2^3*16)与显示精确一致。
Inspector明确AMR cell field arrays Not provided by this API；
普通Init sample3302仍单独显示，未把其场值称为AMR cell averages。

Preview state下hierarchy not constructed是Field请求自身旧快照；
独立AMR区域展示Complete。保留这一现行语义，不合并成不存在的Core响应。
本次只观察UI匹配标签，没有提取认证信息或宣称所有隐藏response bytes已前后核对。

## 正常退出与覆盖边界

正常关闭原生窗口，launcher session54438 exit0。
关闭前精确8个owned PID/startTicks，包括Host1552914和preview-session1554253；
关闭后全部不再存在。输入SHA保持，受管工作树clean，
配置指定output/sedov_standard未创建。未kill其他ARCH终端/进程。
进程列表、launch来源、UI观察及独立几何见同名Summary.json；
本机日志留studio/.local/integration/native-sedov-20261004，原始输出不提交。

本轮无源码改动，不重复329项已过自动baseline。
完成Sedov二维原生field/Inspector、多层AMR显示/几何/正常关闭这一矩阵行。
wheel zoom/pan沿上一轮限制仍未验证；全模型矩阵、演化、独立科学review、
O7待决项、CUDA/O9与整个联合目标仍未完成。未push/tag/main merge或Windows适配。
