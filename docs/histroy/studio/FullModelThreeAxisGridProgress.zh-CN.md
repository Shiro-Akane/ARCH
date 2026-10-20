# 全模型初态实施记录：三轴网格与根容量

2026-10-03；基线583ac18e4b02a01fce76ac17afb8678f3a3ef59b。
本轮只完成全模型初态所需的网格安全边界和真实根拓扑序列化，不是完整场/三维AMR完成报告。

## 改动与所有权

src/api/preview/ResourceEstimates.h统一RootBlockCount和ValidateInitialPreviewGrid。
RootBlockCount计算全部活动轴乘积，先拒绝非法dimension/非正活动轴及int64溢出；
ResourceCounts和BuildInitialMesh共用该计数，不维护另一套三维根容量公式。
原Preview.cpp两轴validate_grid移入同一API资源所有者并补齐三轴；
RequireLoadedValues仍在实际调用边界执行，没有取消配置准备保护。

三轴active bounds、boundary、Morton root extent和配置pool容量在分配前校验。
BuildInitialMesh中原漏掉x3的容量比较修正；
非法/溢出根计数抛错误，不伪装成预算limited。
合法根块超过工作预算仍返回limited/none，不开始初始化。
leaf lower/upper/cellShape/cellSpacing补第三轴，logicalKey仍来自真实树。

算法仍复用InitializeRootState/AmrTree/RefinementThermodynamics/BC/ghost/Regrid；
未修改科学Core模型、EOS、坐标转换、细化算法或物理阈值。
当前public Preview gate仍Sod1D/CellularDet2D Cartesian，
没有因为本轮内部三轴支持就发布新模型/三维capability。

## CPU证据

- 现有build-cpu CPU Release，ARCH_ENABLE_CUDA=OFF；单任务构建。
- 原preview_sampling_limits覆盖三轴root product、正好30/不足29的容量、
  x3零长度/非有限/非法boundary/Morton范围、非法dimension；CTest PASS。
- 新preview_mesh_geometry直接调用共享BuildInitialMesh，几何fixture不调用科学Setup：
  1x1x2 roots、非等长/偏移域；预算1在分配/初始化前limited；
  预算2构造真实两片x3根叶块，bounds[-2,10,7]→[6,16,9]与
  [-2,10,9]→[6,16,11]、logicalKey0:0:0:1、
  shape[16,16,16]、spacing[0.5,0.375,0.125]通过。
  资源metadata rootBlocks2/activeCells8192与实际拓扑一致；
  int_max^3根溢出在分配前错误，初始化计数不增长。
- 新fixture最初namespace引用编译失败，已修正arch::dispatch，未改测试条件。
  最终geometry编译/CTest exit0；两个scoped CTest均通过。
- 生产ARCH的Preview.cpp/ResourceEstimates.cpp对象实际编译exit0，
  覆盖生产EOS模板实例化；不把对象编译称为ARCH完整链接或新binary运行通过。
- git diff --check PASS。Host/Studio未改，不重跑未变化235项。
- 添加CTest导致既有build-cpu内部标准CMake regeneration；没有独立新建/迁移build tree。
  最新补充修改只重编/复测受影响geometry和Preview对象，未重复不变sampling检查。

本机日志/summary：studio/.local/integration/three-axis-grid-20261003。
几何fixture仅根拓扑/lref=0，不声称真实模型Setup、三维细化/守恒或科学演化通过。
无新scientific H5/plt/checkpoint。

## 未完成与下一步

实际GeneratePreview仍由case推旧1/2D采样，完整1/2/3D loop、VELZ、native坐标/单位、
能力声明、CLI/session传输及Host/UI多模型/切片未接通。
下一步以已解析dimension驱动真实Setup/Init/shared EOS采样与有界响应，
逐模型用原测试/验证输入核对；随后真实初始AMR/Studio及独立plt。
现行二维cylindrical(r,phi)不是未来RZ(r,z)，不改物理定义。

build-studio-cpu/bin/ARCH SHA仍e506619f...03813bc7，未重新链接生产binary；
源码及CMake已变，旧binary只代表compiled version，不能宣称source current。
全目标/科学/CUDA验收仍未完成；历史G与architecture迁移审批独立pending。
本轮无simulation/CUDA/push/tag/main merge/Windows适配。
