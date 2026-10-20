# BurnGradient Linux production desktop 初态验收

启动源码278ff187bfbd7c851558140b2f961dd9a4e081b9，production代码584508ea；
managed clean源码64b0ce2f8d97553f59024978618f1e4848a974e1。
CPU ELF d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75，
Build92d429da-42d9-4336-b956-79c43440b5c3；完整依赖freshness仍unknown。
复用既有合法BurnGradient输入，不改配置/源码，不Configure/Build/Run/Save。

## 原生界面

窗口31590792，真实1D Cartesian x1=[0,1]cm，512 Init samples。
启动期间自行preparing/current，不记手动首次Generate验收。
均匀density1e7、温度脉冲可见；范围UI5e7–2.99956e9K。
实际图选中心sample255、x=.4990234375cm，右Inspector UI舍入值：
rho1e7、P1.8694523e24、T2.9995604e9、VELX0、
ENER4.0916506e24、EINT4.0916506e17。
沿authoritative Init同一公式的独立算术对照T=2999560448.494734，
不是独立EOS科学oracle，不能由此确认燃烧演化。

实际Inspect initialization Current：background/peak/center/width/rho来源explicit；
缺失组分默认、输入显式值与最终raw Init的smallx floor分别显示。
3个raw probes，本轮完整查看首个(.25,0,0)cm：
DENS1e7、TEMP50169303.12178143 consumed、PRES0 unused by conversion、VELX/Y/Z0。
13个index/name表均可见，c12/o16各.5，其余1e-20；
UI没有额外归一化、clamp或添加默认。
这是EOS转换前稀疏Init，不冒充连续组分场。
菜单只有6个fluid字段，没有continuous species/ENUC，本轮不作这些项PASS。
没有逐个验收其余2个raw probes。

## 初始AMR及退出

一次Generate initial AMR Current/Complete，1leaf L0，0completed passes，
配置max_blocks/working capacity均8，active cells16。
选块0:0:0:0：bounds[0,1]cm，cellShape16，spacing.0625cm；
与1/16算术一致，field/config/build/EOS/native坐标identity界面匹配。
资源表base4608、withSpecies14592 bytes，pool base36864 bytes仅规模估计；
不是OOM预测或正式模拟就绪认证。
AMR cell field arrays明确Not provided；普通Init Inspector不作AMR cell平均值。
field状态中的hierarchy-not-constructed与独立AMR结果分别保留。

正常关闭launcher exit0；8个已捕获owned PID/startTicks均消失。
输入字节不变、指定科学output不存在、managed worktree clean。
原始输入和日志留本机ignored .local；只提交处理后报告/Summary。
本轮无源码修改，不重复已过331项自动baseline，不push/tag。
全模型native矩阵、科学CPU/Jeans/RZ、CUDA和O9仍未完成。
