# CooperativeHotspots Linux 原生二维验证

联合计划第5项：全模型初态／AMR，新增该模型native矩阵行。
原生production Electron+owned Node Host，不用browser/Vite替代。
launch source1011bf113ad5a0eb1d6b54a38d0912bef72fc287，
managed source64b0ce2f8d97553f59024978618f1e4848a974e1，
CPU SHA d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75，
Build92d429da-42d9-4336-b956-79c43440b5c3。完整依赖freshness仍unknown。
准确config/EOS身份见Summary；原有效配置逐字节复制，未调整物理或阈值。

## 原生结果

128×128，shape[128,128]，x1-fastest，x3=0cm，区域x[0,64]/y[-24,24]cm。
Density与Temperature实际切换显示；temperature范围2e8–4e9K。
native hotspot sample8127=63*128+63，位置[31.75,-.1875]cm；
rho20318492g/cm³，T4e9K。Ambient sample13081=102*128+25，
位置[12.75,14.4375]cm，rho4e7、T2e8。
二者显示P6.1774055e24erg/cm³、VELX/Y0，符合模型等压初态的界面语义。
这些为UI舍入值，不是独立EOS求解oracle，也不证明燃烧演化正确。

来源代码Setup使用共享GetPressureFromRhoT、网络组分与等压密度求解；
Init提供静止primitive与温度/组分。没有在Studio复制模型公式。
状态区helmholtz/ready，加载真实配置所指helm_table.dat，13species；
表SHA c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1。
EOS configuredGamma1.4为无关配置字段，不能解读为本次IdealGas fallback。

InitialAMR Complete，12个L0 block，资源表3072activecells，pool256未改；
图有真实block outline/cell lines。资源估算明确不作OOM保证。
列表选0:3:2:0：lower[48,8]cm，upper[64,24]cm，16×16，
spacing[1,1]cm，与4×3根网格独立算术一致。
AMR cell field arrays“Not provided by this API”，初态采样不冒充cell field。
field Preview原先的hierarchy-not-constructed快照与独立成功AMR响应分开；
matching identity是UI标签证据，未提取authenticated Host或不可见响应数据。

## 收尾与范围

正常点击自建窗口X，launcher80877 exit0，8owned PID/startTicks全部消失；
配置SHA不变，configured output/cooperative_hotspots不存在，managed worktree clean。
未Run/Restart/simulation、未Build、未修改源码、未重复已有329自动测试。
没有再次wheel注入；此前native zoom/pan仍UNVERIFIED。
原始日志本机ignored目录，提交处理后摘要和说明，不push/tag。
本轮fetch确认compute/optim与codex/o8-boundaries无新提交，科学待决仍有效。
剩余全模型native矩阵、科学CPU出口、CUDA与批准O9均未闭合，整体目标未完成。
