# SNIaCoupled：二维/三维四模块联动示例

本算例以 **CGS** 单位设置 C/O 燃料和光滑温度热点，由正式 Driver 联合运行
Hydro、自引力、aprox13 核反应及热扩散。算例源码只引用
`<UserInterface.h>` 和 `<GlobalDefs.h>` 两个公开头。热点是厘米尺度的耦合执行
检查，**不是**完整白矮星、火焰或 SN Ia 爆轰模型；不据此推断天体尺度的物理精度。

从仓库根目录运行，例如：

```sh
./build-ci/cpu/bin/ARCH SNIaCoupled simulation/SNIaCoupled/SNIaCoupled_2d_polar_amr.par
```

配置文件按原生坐标和验证用途分组：

| 文件 | 域与用途 |
| --- | --- |
| `SNIaCoupled_2d_cartesian.par` | 二维 Cartesian 全周期、无 AMR；保留原有 120 步 CPU/CUDA 基线 |
| `SNIaCoupled_2d_cartesian_amr.par` | 二维 Cartesian 全周期、混合 AMR |
| `SNIaCoupled_2d_polar_amr.par` | 二维完整方位角极坐标、正内半径、isolated 重力、混合 AMR |
| `SNIaCoupled_3d_cartesian_amr.par` | 三维 Cartesian、isolated 重力、混合 AMR |
| `SNIaCoupled_3d_cylindrical_amr.par` | 三维柱坐标，避开轴线，完整方位角，混合 AMR |
| `SNIaCoupled_3d_spherical_amr.par` | 三维球坐标，避开原点与两极，完整方位角，混合 AMR |

完整方位角且包含原点、轴线或两极的 CPU/CUDA 输入见
[P12 验证样例](../../validation/gravity/curved/inputs)；它们复用本算例，
不另建一套物理实现。二维极坐标的 Poisson 势对应沿第三方向平移不变的物质，
使用单位长度质量和对数核；它不是三维孤立白矮星。三维曲线坐标使用有限质量 Newton 势。
曲线坐标自引力已按受测范围开放 **CPU/CUDA、完整方位角及坐标奇点接合**，
奇点流体面须 reflecting；部分方位角扇区仍被拒绝。`rho0`、`temperature0`、`temperature_peak`、
`density_amplitude`、`hotspot_width` 和 `center_x/y/z` 均为 CGS 场景参数；
热点在物理 Cartesian 坐标中定义：

\[
q=\exp\left(-\frac{|\mathbf{x}-\mathbf{x}_c|^2}{2\sigma^2}\right),\quad
\rho=\rho_0(1+Aq),\quad T=T_0+(T_{\rm peak}-T_0)q.
\]

验收时检查接受步数、`state_repairs.txt` 的 `events=0`、
`gravity_solves.tsv` 中每次求解的 `residual <= target`，以及最终 plot 的
`GPOT/GAC*`、温度、能量和物种场。`dt_diff` 有限表示热扩散参与步长估计；
单靠此算例不能证明独立扩散误差。Helmholtz 表只用于已支持的热扩散路径，
不据此声明物种扩散。详细验证、与用户 FLASH 归档中 Cellular 算例的受控比较及限制见
[重力验证记录](../../validation/gravity/README.zh-CN.md)。

这些输入实际采用 HLLC、MUSCL/MC、RK2、RKL2 热扩散、BD/DenseLU、复合
多重网格和 AMR，配 Helmholtz/aprox13 并关闭 NSE；固定网格输入单独标注。
它们是该组合的 CPU/CUDA 执行证据，不覆盖全部方法替换。`Setup` 要求四模块
同时开启，不能直接关掉一个开关做消融；独立算子检查使用对应验证入口。
Helmholtz 的当前恒星输运只提供热传导，不能靠打开黏性或组分扩散开关获得完整
输运。其他适用条件见[组合规则](../../docs/Reference.zh-CN.md#方法与物理模块的组合)。
