# 全模型真实初态生成接线记录

2026-10-03，基线8522e85b。按联合交付计划推进全模型初态；本记录不是Studio全模型出口或科学验收报告。

## 实现与边界

实际GeneratePreview现在由解析后的配置维度驱动1/2/3轴采样，
复用SetupChecked、SampleInitialPrimitive、InitialConservedState、共享EOS及实际InitialMesh。
没有复制模型物理、修改科学Core公式、阈值或G定义。
域策略集中在现有Preview所有者，生成、能力与runtime registry discovery共用；
未知注册模型不自动获得完整场能力，已审阅域仍须通过模型Setup的具体配置检查。

旧Sod/Cellular顶层能力、字段顺序、Cartesian sampling和真实字段回归保留。
新增独立initialSampling/coordinates版本1，三维shape=[Nz,Ny,Nx]，
index=(k*Ny+j)*Nx+i，新增VELZ和八字段精确EOS缓存行。
CLI增加--samples-x3，warm session增加samplesX3；配置错误与后续请求恢复保持真实身份。

坐标/单位消费Grid和共享presentation所有者。
球/柱native轴的角单位为rad，速度为native orthonormal，
显示名也按原生轴命名，不能将径向速度称为Cartesian X速度。
2D cylindrical仍(r,phi)，不是RZ；spherical 2D仍赤道面。
曲线AMR不提供单一cm单位，data.unit=null并提供逐轴coordinates.metadata。
BurnOneZone明确uniform-state，不把重复采样解释成反应轨迹。

首次实际探针发现JeansWave/SmoothAdvection/ExternalGravity的species为空；
原Preview两模型边界错误拒绝。共享IdealGas本来就支持零species，
本次仅移除该错误API拒绝，不给模型添加假species、不改Cv公式或物理默认。
非Ideal零species继续明确失败。

## 真实CPU验证

现有build-cpu Release/CUDA OFF实际编译并链接ARCH。
8任务并发、既有内存保护器、2 GiB余量、swap增量上限256 MiB及PSI保护；
首轮最低可用约16.6 GiB、owned RSS峰约4.9 GiB，无swap增长。
最后native-label增量编译exit0，无swap增长。

- 14个维护中的有效case输入：真实字段+实际单根AMR构造通过。
  字段两个bin中心与inspect-case四分之一/四分之三真实Init探针逐点对照；
  验证species、有限值、6/7/8字段、shape、配置SHA及无文件输出。
- Gaussian非立方体[2,3,5]：Cartesian、spherical、cylindrical逐点核对
  native→Cartesian映射、非对称高斯场、VELX/Y/Z、轴单位与显示名。
- 三维代表：Sedov、RT、显式周期GravityBox cube，以及仓库SNIaCoupled的
  Cartesian/cylindrical/spherical参考输入，字段与真实三维根leaf几何通过。
- 曲线1D/2D：固定坐标、angular单位及AMR逐轴单位通过。
- warm session：非立方体shape切换、与single-shot完整data等价、
  不完整三轴请求失败后恢复Sod、无output通过。
- exact sample cache目标实际重编，VELZ输出保留、w一ULP变化失效、
  零species与原有精确缓存边界通过。
- 最后代码修改后的preview_api_contract、preview_full_model_contract、
  preview_session_contract、preview_cellular_2d 4/4 PASS。
  参数metadata、sampling limits、root geometry、case inspection及verified resources
  此前相关检查通过；未因纯显示名修改重复这些不变检查。
- diff-check及提交白名单检查作为收尾执行。Host/UI未改，不重跑未变235项。

新测试最初leaf字段名拼错、旧Cellular断言固定两个模型、三维GravityBox工程fixture缺轴输入；
均修正测试/输入而非放宽生产校验。完整失败和替代通过日志留本机。
本轮真实API调用不进入Driver timestep、不生成H5/plt/checkpoint或正式output。
测试生成完整响应未上传，提交仅指标/身份/域摘要。

## 身份与未完成项

CPU测试binary SHA: 7ca561d57b876cbdc7fa4672199278682ee8a174a89cf13c6df3ab2a5421e9b7。
Studio当前生产binary仍e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7；没有移动production profile或伪造其Manifest current。
源码/CMake增量必须由后续受管Build建立新的binary/manifest身份，再接入Host/UI。

AMR矩阵本轮是实际root构造/lref=0，不是全模型混合细化、守恒或演化科学认证；
mesh没有cell field arrays，普通Init Inspector不是AMR cell value。
Host validators/provider、通用模型选择与三维切片/单区视图、production desktop UAT尚未完成。
独立plt读取、JENS/RZ、CUDA/科学短测与批准长轨迹仍待后续。
历史特殊G换算与architecture迁移审批仍单独pending。

完整日志本机studio/.local/integration/full-model-generation-20261003。
未push/tag/main merge，不开展Windows适配。
