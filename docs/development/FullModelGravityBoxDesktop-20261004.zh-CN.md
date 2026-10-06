# GravityBox Linux production desktop 定向验收

## 范围与身份

本次只验证真实 Cartesian 1D Init、参数可达、AMR 几何及正常退出，不执行 simulation、Poisson 演化验收、Configure/Build 或 Save。
Studio launch baseline b625728717ec269cac875e0394203bd99226fd5c；
managed source 64b0ce2f8d97553f59024978618f1e4848a974e1。
生产 assets 复用已通过的构建；独立 clean CPU ELF SHA-256
d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75；
Build ID 92d429da-42d9-4336-b956-79c43440b5c3。完整依赖 freshness 仍 unknown。

配置从已验证输入逐字节复制，SHA-256
524bd50ae34e1408cf79b05159424264c6b92b351e64fa52971b6b95dc642fc5。
原始配置、launcher 输出、进程记录留在本机 ignored .local；不提交原始科学数据。

## 实际原生 UI 观察

独立窗口 62849886 手动点击 Generate Real Preview 后显示 Current；
512 个样本、区域 x1=[0,1e8] cm，Density 范围 9.99000e6–1.00100e7 g/cm^3。
搜索 gravity_type 后 self 控件可用；未编辑参数。字段菜单为 Density/PRES/TEMP/VELX/ENER/EINT，
没有 GPOT/GAC；self 可配置不代表 gravity field Preview 支持。

点选 index99：显示 x=19433594 cm、rho=10003427 g/cm^3、
P=8.3173117e21 erg/cm^3、T=1e7 K、VELX=0、
ENER=1.2475967e22 erg/cm^3、EINT=1.2471694e15 erg/g。
这些是 UI 舍入值；sample center 独立算术为 19433593.75 cm，不作为独立科学 oracle。

Initial AMR 显示 Complete、4 个 L0 leaf、64 active cells、completedPasses=0，
配置/工作容量均64，无 budget limit。资源估计 base18432 / with-species21504 /
pool294912 bytes；不涵盖 self-gravity workspace，不作 OOM 预测。
选择块 0:3:0:0：level0、logicalIndex[3,0,0]、bounds[7.5e7,1e8] cm、
cellShape16、spacing1562500 cm，与 (1e8/4)/16 算术一致。
API 明确不提供 AMR cell field arrays；Init sample Inspector 与 AMR geometry 分开。
Preview field snapshot 的 hierarchy-not-constructed 与独立 AMR 成功结果分别保留。

## 清理与限制

正常点击窗口关闭，launcher session99083 exit0；
8 个本轮 owned PID+startTicks 全部消失。输入 SHA 不变，配置 output/gravity_box 不存在，
managed working tree clean。没有重复不变的自动 baseline，未 push/tag。
本轮未做 wheel/pan；既有 input routing 限制仍未验证。UI Gravity 导航跳转未列入 PASS。
科学 Core review、完整模型矩阵、Jeans/RZ、CUDA/O9 仍未完成。
