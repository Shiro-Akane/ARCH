# 实际 Driver → RZ Plotfile：内部接线

## 修改与边界

基线8f018f12。实际DriverIO::write_plot此前未传Runtime显式profile，
使RZ中心走旧polar、VORT/DIVV走旧二维基底且忽略二维第三速度分量。
现使用同一GridMetrics::GeometrySemantics传write_plt和HDF serializer，
坐标与diagnostics均复用当前shared owner，默认Existing仍保持旧能力。

未知profile及RZ错误dim/geometry在创建输出前拒绝。没有依据geometry字符串
自动把旧cylindrical文件解释为RZ，没有开放公共RZ/runtime/API能力。
科学数值方法、独立预算、EOS与checkpoint格式均未改变。

## 内部文件候选语义

- 根geometry=cylindrical、dim=2；显式geometry_semantics_revision=1、
  geometry_chart=axisymmetric-rz，独立于配置/软件/checkpoint版本。
- Data FP64 [B,Ny,Nx]，i最快，无ghost，仍是实际活动叶顺序。
- Grid/x,y,z为子午面代表位置(r,0,z)，不是将环体质量替代为点源的gravity模型。
- NativeGrid/version=candidate-axisymmetric-rz-1；x1_axis=r_cy、x2_axis=z_cy，
  x3 inactive，坐标单位cm。lower/upper与field展平一致，
  cell_measure来自shared GridMetrics::CellVolume。
- measure_unit=cm^3、measure_normalization=full_rotation，
  measure_convention=full-rotation-axisymmetric-ring；不能标为Cartesian二维单位横向长度。
- VELX/Y/Z为径向/轴向/方位速度，即使dim=2也保留请求的VELZ；
  basis=local-orthonormal-r-z-phi，meaning分别radial_velocity/
  axial_velocity/azimuthal_velocity，单位仍使用共享CGS cm/s。
- VORT/DIVV调用同一显式RZ GeometryView。场/来源身份及checked-close→atomic-rename不另造数学。
- Cartesian metadata/version保持candidate-cartesian-1，其中心、低维测度和字段语义保持。

这只是供O7.5内部review的候选写出格式，不要求首版Cartesian owner adapter提前支持。
现有Studio Reader明确拒绝该RZ native版本；没有静默按Cartesian/polar显示。
RZ Viewer需等待整条路径和支持域验收后单独迁移。

## 实际 CPU 证据

复用真实Driver/Runtime/IO checkpoint fixture，补充PlotIO.cpp实际编译，
fixture ELF及9个TU/显式shared header SHA见同名Summary。
RZ domain r[0,1], z[-4,4]，16×16，time=0/step=0。
这是分析场fixture，不是新注册物理case或科学演化：
rho=2、ENER=100、v_r=.1r、v_z=.2z、v_phi=.3r；
实际Driver在写前materialize并施加现有physical ghost规则。

独立h5py/80位Decimal读回通过：
- 全256cells坐标/三速度原数组精确一致；
- 完整domain体积8*pi，cell measure最大relative误差2.0561161577137884e-16；
- DIVV内部解析及全域边界离散maxabs7.216449660063518e-16；
- VORT内部解析及全域边界离散maxabs1.1102230246251565e-16。
沿用内部工程2e-12算术gate，不当作维护者科学预算。

首轮独立对照错误地将continuum结果用于outflow边界，失败保留；
实际边界值依照复制ghost stencil验证，未改算子或放宽阈值。
轴线r/phi奇映射与z边界outflow分别纳入独立参考。

Cartesian/RZ实际checkpoint/report失败恢复回归保持PASS。
既有arch_plotfile_publication重编+CTest 1/1 PASS，保留默认Cartesian发布反例。
真实隔离Reader接受Cartesian fixture、明确WORKER_FAILED/
Unsupported candidate native geometry拒绝RZ；原文件SHA不变。

## 待完成

主ARCH尚未为本次IO修改重编；不能将fixture SHA当作主binary。
没有公共RZ、AMR角动量迁移、有限环体引力、真实演化、CUDA或新desktop UAT验收。
原H5/checkpoint/ELF/logs在ignored studio/.local；提交源码、工具与处理后摘要。
复现：run_driver_checkpoint_geometry.py、verify_rz_driver_plot.py、
verify_rz_plot_reader_gate.mjs。没有push/tag。
