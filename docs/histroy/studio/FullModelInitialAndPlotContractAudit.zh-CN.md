# 全模型初态／AMR与独立Plotfile：进入实现前契约审计

2026-10-03；基线d81620ad817af9c2edb048b1260bfcc3b00c3ed5，工作树clean。
依据StudioConfigurationHandoff.zh-CN.md第5节第5项及独立plt出口。
本报告是实施审计，不是全模型Preview、AMR或plt完成报告；不引入新的发布阶段编号。

## 真实状态与必须迁移的限制

对当前CPU binary调用--list-cases/--preview-capabilities，均exit0。
14个注册模型均声明primitiveSinkProbe；目前只有Sod1D/CellularDet2D公布完整场与AMR能力。
注册/点初始化检查不等于完整场支持，也不等于simulation就绪。
binary SHA e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7。

| 所有者 | 当前限制 | 实现动作 |
| --- | --- | --- |
| src/api/inspection/Discovery.cpp:19 | 两模型硬编码、固定维数 | 注册信息与已实现有界初始化能力共用来源，保留逐配置Setup校验 |
| src/api/preview/Sampling.h:36 | case名决定1D/2D | 由已解析配置维度确定shape、在分配前校验轴/总数/序列化预算 |
| src/api/preview/Preview.cpp:70 | 两元素blocks/bounds/boundary数组 | 支持第三轴前补齐；不能仅删除support gate而造成3D越界 |
| src/api/preview/Preview.cpp:147 | Cartesian两模型gate | 复用SetupChecked/SampleInitialPrimitive/shared EOS，逐模型域验证 |
| src/api/preview/Preview.cpp:225 | 两轴循环、固定6/7字段 | 三轴顺序、VELZ、共享坐标/unit metadata与原始字段 |
| src/api/preview/InitialMesh.h:43 | roots未包含x3 | 容量乘积/预算检查包含全部活动轴，溢出先拒绝 |
| src/api/preview/InitialMesh.h:108 | bounds/shape/spacing仅第二轴分支 | 输出真实1/2/3D叶块；native坐标明确，不假称Cartesian物理边界 |
| studio/host/previewProfile.ts | 两个静态profile | 可信binary capability＋当前配置决定可用Preview；不由文件名猜模型 |
| studio/src/host/previewValidation.ts:21/45/96 | case推维数、最多两profile/两轴 | 按协商后的请求/响应维度及版本验证，保持字节/身份/race保护 |
| studio/host/previewRunner.ts:193 | 只接Sod metadata | inspect-case metadata独立保留，扩展绑定不得猜测；已有Sod绑定回归 |

当前support gate使3D不可达，所以两元素数组是扩展前须解决的风险，不宣称当前可调用3D发生越界。
AMR继续复用InitializeRootState、RefinementThermodynamics、BCHandler、ghost exchange与Regrid。
不复制物理初始化、EOS、引力/反应/扩散、几何或AMR算法。

## 逐模型源码域与输入来源

以下是源码/README审计，不是逐模型成功Preview的证据；实际配置需通过当前v3检查与SetupChecked。
不从Get默认数字创造新的已批准科学输入。数量14只记本次binary，不写生产常量。

| case ID | 现有模型约束/显示需要 | 验证输入来源 |
| --- | --- | --- |
| Sod | 1D Cartesian，现有x_pos绑定 | simulation/Sod/Sod.par |
| CellularDet | 当前2D Cartesian/shock_dir0/1；其他域不因本审计自动开放 | simulation/Cellular/CellularPreview2D.par |
| BurnGradient | 1D Cartesian、burn开启，真实温度梯度 | tests/api/inspection/test_case_inspection.py及已有燃烧验证输入 |
| BurnOneZone | 1D Cartesian、burn开启；Init忽略位置，均匀one-zone状态展示，不冒充空间结构 | tests/api/inspection/test_case_inspection.py；validation/backend/cases.json |
| DiffusionMode | 1D Cartesian、组分范围由Setup判定 | tests/api/inspection/test_case_inspection.py及原验证输入 |
| ExternalGravity | 1D Cartesian、external gravity | tests/api/inspection/test_case_inspection.py及原验证输入 |
| SmoothAdvection | 1D Cartesian、正密度/mode/L条件 | tests/api/inspection/test_case_inspection.py；validation/amr/gpu_cases.json |
| Gaussian | Init使用Grid转换后的Cartesian点，原生几何和native velocity区分 | simulation/GaussianPulse/Gaussian.par及validation/amr原生几何输入 |
| GravityBox | Cartesian/cylindrical/spherical；isolated Cartesian要求3D；radial条件由Setup判定 | simulation/GravityBox/GravityBox.par及现有曲线坐标输入；历史非物理G输入待批准 |
| JeansWave | Cartesian IdealGas/self、稳定分支；使用共享G、root-cell平均修正 | simulation/JeansWave/JeansWave.par；稳定分支不外推成任意模式已支持 |
| RT | README定义2D/3D Cartesian分层初态；不根据缺少Setup拒绝推断1D科学支持 | simulation/RTinstability/RT_instab.par |
| SNIaCoupled | 2D/3D Cartesian及现行curved；Setup要求四模块，不关模块做初态捷径 | simulation/SNIaCoupled现有分域.par |
| Sedov | Cartesian，1/2/3D参数/能量条件 | simulation/Sedov/Sedov.par及已有不同维数基准 |
| CooperativeHotspots | 2D Cartesian；isobaric EOS求解保留 | simulation/CooperativeHotspots/CooperativeHotspots.par |

BurnOneZone虽然注册point initializer，Init是uniform state；显示状态卡/组分与当前请求身份，
不能用均匀曲线暗示反应已经演化。完整AMR只对实际空间初始化语义提供清晰能力状态。
现行二维cylindrical是(r,phi)，三维是(r,z,phi)；二维spherical为(r,phi)赤道平面。
使用Grid::PhysicalCoordsFromNative与CoordinateMetadata，绝不把二维cylindrical改标签成未来RZ(r,z)。

## 最小实现顺序和候选契约

1. 先在当前采样所有者实现按已解析dimension的有界1/2/3D计划，保留旧Sod/Cellular调用，
   补三轴grid验证、所有活动轴root容量计算，再接实际GeneratePreview循环。
2. 初始化仍经过RuntimeParams InitialState目的、SetupChecked、SampleInitialPrimitive、
   InitialConservedState及实际EOS；共享species与source validation。
   首批有效参考输入逐模型测试，失败状态/缺表/不支持域保留原响应身份，不以改物理输入求通过。
3. 能力声明增加明确的配置校验/支持域与表示说明；不能把所有registered case自动标为已测试全域。
   现有schemaVersion=1.0与旧1D/2D字段语义不变；新的3D/表示能力用独立版本可选扩展协商，
   未理解扩展的客户端拒绝该表示，不能误按CellularDet二维解析。
4. 候选三维shape=[Nz,Ny,Nx]，index=(k*Ny+j)*Nx+i，x1-fastest；
   三轴由Core给name/displayName/unit/values、geometry和native velocity basis。
   初态点值仍不是AMR单元平均值。三维展示以有界切片/选层与原始Inspector实现；
   slice/zoom只显示现有数据，不重复Setup/Init或改Config。
   3D采样上限须经真实8MiB响应与内存测试后才发布capability，不能直接套2D总点数。
5. AMR支持三轴真实bounds/cellShape/cellSpacing、logicalKey/level；limited仍不是complete。
   current field/AMR project/case/configRevision/build/binary/EOS identity必须匹配。
6. Host/UI完整协商：动态profile、config-dimension、多模型、state表示、三维切片，
   保留取消/旧请求淘汰/旧成功保留/no-auto-save/no-auto-preview及Sod marker。
7. 完成逐模型CPU scoped检查、Host/Studio回归、production UAT；科学范围和未支持配置逐项标注。
   之后再进入O7.1 JENS，不提前编译CUDA。plt独立交付，不阻塞命令行科学验证。

任何额外Setup scientific规则、历史G相似换算、O7.4边界/数学选择依旧交维护者。
本审计没有改变这些定义或自动批准尚未通过的architecture迁移。

## 真实Plotfile只读审计

现有src/io/plot/PlotIO.cpp：
- Data/<field> shape=[block,Nx]、[block,Ny,Nx]或[block,Nz,Ny,Nx]；
  active-block顺序，块内k/j/i且x最快，排除ghost；不是必须Morton排序的checkpoint顺序。
- Grid/x/y/z为扁平的真实Cartesian cell center，来自Grid::GetPhysicalCoords。
- Grid/level、Grid/morton为每块元数据；morton不是完整portable logicalKey身份。
- selected field只写请求变量，PRES/TEMP用真实EOS转换，species是X，附加场可能来自已实现所有者。
- HDF5Writer写root time/dim/geometry；目前没有case/config/build/binary、field units、native bounds声明。

使用系统已安装libhdf5_serial只读既有本机Sod_HLLC_plt_0004.h5，
只读time/dim属性及dataspace，没有读取完整场数组：
time0.2/dim1，Grid/x/y/z shape4096，level/morton256，四个field shape[256,16]。
caseId/configRevision/buildId/binarySha256/schemaVersion root attrs均不存在。
源文件仅留本机；元数据摘要见同名Summary.json。

独立plt读取接口必须：
- 只读，选定本机项目/文件并核对file fingerprint，bounded datasets/hyperslabs、响应/读取预算和取消；
- 数据身份与当前配置/Preview分离，缺乏file内身份时明确unknown；
  不从文件名或附近.par猜case。已知Run manifest关联也仅在file/path/fingerprint确证后声明；
- 不把坐标逆变换自动当作权威native cell bounds。Cartesian均匀小例可验证重建；
  curved/singularity/native bounds不足的情况明确支持边界，必要Core格式扩展单独review；
- 原生单元Inspector保留原始数值；LOD仅绘制，不能拿平均/插值数据冒充cell原值；
- 不重算EOS填补缺失字段，不将checkpoint/历史不同格式当plt无验证读取；
- 无原始H5/plt/checkpoint或完整数组上传，处理后元数据/指标才提交。

plt与checkpoint格式身份不同，不能把checkpoint format6套到没有格式属性的plot。
读接口不需要修改科学writer来伪造历史身份。

## 本轮范围与下一动作

本轮只读查询和本机持久审计文件，无新Setup/Init/AMR/simulation/Build/CUDA。
不重跑已通过的235项不变回归；未修改Core/Studio实现。
下一原子实现是采样计划/三轴边界/容量的共同支持，随后依上述顺序接真实Core响应和Studio。
历史G输入及architecture-audit审批独立保持pending；不转向Windows。
