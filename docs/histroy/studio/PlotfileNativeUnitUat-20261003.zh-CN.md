# Plotfile 新单位声明 Linux 原生 UAT

## 引用和范围

writer 构建来源 ab23bad7e8bea93c3569618d1a6335312372cc30；
Host/Viewer 实现 e7181420d06f3eb95eff85061d347ed0f26eaec6。
production assets index-BJQ3bLmF.js，真实 Linux/WSL Electron 独立窗口；
不是浏览器 DOM/SSR 或 HTTP 验证代替桌面操作。
旧窗口正常关闭后确认进程消失，再启动现有 launcher；新窗口 PID 788183。
只读取上一轮已经生成的 Sod/CellularDet t=0 H5；本轮没有 Build、Preview 或 simulation。

## 实际操作和结果

- Sod：Real Plotfile → Read metadata → global LOD → 图上点击 → 原生 Inspector。
  x1=cm，DENS=g/cm^3，time=s；记录字段 mass_density / scalar。
  点击物理位置约0.6521133，返回 block3/global51/local i3，
  中心0.65234375，raw DENS0.125；measure0.0078125 cm，
  normalization=per_unit_transverse_area。inactive bounds均0。
- CellularDet：替换实际文件路径 → metadata → global LOD → 图上点击 → Inspector。
  20×16×16，5120叶单元扫描，32×24显示像素；x1/x2轴cm，
  显示均值range标g/cm^3，20叶块轮廓与原始文件同digest。
  点击位置约(0.47897,6.49172)，返回 block12/global3074/local(2,0,0)，
  中心(0.5,6.5,0) cm，raw DENS43375362.843074 g/cm^3；
  measure0.040000000000000015 cm^2，
  normalization=per_unit_transverse_length。
- 两个实际选中原生单元由独立h5py直接读取再次核对：值、中心、测度、文件SHA一致。
  Summary只保留处理后的点记录，不提交H5或原始场数组。
- LOD的均值/全部叶扫描成本提示、partial source evidence、completion unverified、
  run/effective/build/source Git unknown仍实际显示；没有提升科学认证状态。

## 本次覆盖限制和 findings

本轮仅完成新单位标签和两个原生单元回查；之前导航UAT是独立记录。
没有声称所有字段、wheel、cancel/race、失败保留和大文件/index/cache完整验收。
真实文件当前仅输出DENS，其他producer声明不等于已有全字段生产证据。
Cellular per-cell bounds对Preview的1.7763568394002505e-15差仍交owner review，
不增加科学容差。

启动Real Config页还观察到两项待排查的诚实状态问题：
stale selected binary已禁止Preview，却存在“tracked inputs validated”旧Build文案；
registry在初始化成功后可能被轮询显示成unavailable。
这两项不能解释为当前binary fresh或完整registry验证通过，本轮未顺手改功能。

309项测试/lint/typecheck/build属于e7181420已有通过证据，源码未改变不重复运行。
本次report-only变更做diff检查；完整联合交付目标继续未完成，不封箱、不push/tag。
