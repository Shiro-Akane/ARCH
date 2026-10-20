# Plotfile 跨块候选显示 LOD（2026-10-03）

对应联合计划 3C 后独立 plt 排期；仍是待 review 的小范围实现。总目标 O7/O9 未完成，未声明正式科学 Viewer 或 Linux 手工验收通过。

## 实现与科学边界

- 新增受控 POST /api/plotfile/audit-overview；请求仅含 projectId、relativePath、expectedFileSha256 和 overview={field,width,height}，每轴 1..32；1D height=1。无任意 command/program/env。
- 沿用路径保护、Linux pinned FD、读前后 inode/mtime/ctime/size、完整 digest、候选已发布 NativeGrid 验证。legacy/非 Cartesian 1D/2D 拒绝，不猜坐标。
- 第一遍以最多 512 单元片段扫描 bounds 得完整 domain；第二遍读取全部叶单元的 selected field/bounds。相邻完整行尽量合成最多 512 单元矩形，宽行按 x1 分段；不取完整 dataset getter。
- native cell 与像素在活动坐标中的 overlap 作为显示权重，输出 coordinate-overlap-weighted-display-mean。这不是 cell_measure 科学积分，也不宣称守恒分析数据。原始场值、精度、科学 Core、checkpoint、EOS 未改。
- 非有限原生值覆盖像素标 null；空像素/显示溢出也标 null，UI 明示 magenta/1D gap，原始值可回查，不做 abs/epsilon/fill。
- 每像素记录 largest-overlap representative native global index，ties 保留先遇到索引。点击回查代表单元，不宣传为精确鼠标位置 cell。Inspector 按同 digest 的 audit-slice 回查 raw value/bounds/measure/file-local key。
- 全域 SVG、字段选择、zoom/pan/Fit；显示只重绘已有 LOD，不自动查询、不改 Config/Save/Preview。尚无 finer-on-zoom。
- 共用单隔离 worker，15s timeout、64KiB stdout、512 单元临时 slice、现有 64MiB 文件限制保持。取消/断开后结束 owned worker 再释放容量；JS heap cap 不是 WASM/RSS 硬上限。
- UI 保留 candidate、单位/完整科学身份未知、scannedCells 和 DISPLAY_LOD_NOT_NATIVE_VALUES/FULL_LEAF_SCAN；renderEligible=false 不改成认证成功。

## 真实 t=0 文件链路

Host loopback HTTP → client validation → React SVG SSR → representative index → 同文件 native Inspector：
- Sod：12 leaf blocks、192 单元，完整 x1=[0,1]，32×1 显示，HTTP 2629 bytes。
- CellularDet：20 leaf blocks、5120 单元，完整 x1=[0,25.6]、x2=[0,12.8]，32×24 显示，HTTP 19580 bytes。
- SHA 与先前 t=0 记录匹配；读前后 H5 完全一致。旧 digest/额外 command/GET 被拒绝。
- 没有科学程序启动或新 output。SVG SSR 不等于 Linux 原生窗口 UAT；32×24 是非方形显示采样，不冒充非方形 native block 验证。

## 读取成本发现与处理

逐行 slice 的 Cellular rchar=34271170 bytes；合并相邻行后=3551170 bytes，read syscalls=1717→217。两份真实 H5 的完整 response 在优化前后完全一致，没有增加数值容差。

最终独立进程测量：
- Sod：返回 2454 bytes、rchar 97786 bytes、peak RSS 116984 KiB。
- CellularDet：返回 19397 bytes、rchar 3551170 bytes、peak RSS 123200 KiB。
- rchar 是整个 query 的 process logical reads（含其他读取），不是纯 HDF5 dataset counter；本次 read_bytes=0，是缓存条件。没有 drop caches 或冷读取速度声明。
- 每查询仍完整 digest 扫描，几何两遍扫描仍存在。小文件不证明大型数据资源边界；固定像素只限制输出，首次总览可能扫描大量叶块。64MiB audit 不宣传为大型 production Viewer 支持。

## 检查与剩余工作

294 项 Studio/Host 测试 PASS；lint/typecheck/production build PASS。8 项 overview scoped tests 覆盖严格请求、显示聚合、nonfinite、非方形 x1-fastest、legacy 拒绝、无 full getter、取消/容量、项目/digest/caller保护、nx>512 和 partial slab。
build 有已有大 chunk 提示，无打包优化。

仍需跨块 native AMR outlines/level 过滤、坐标点原生回查、viewport finer query、有界缓存/index、大型读取消/RSS证据、Linux 窗口真实 UAT 和 owner 科学语义 review。
不扩展 curved/3D/XDMF，不运行 evolution/CUDA。H5/HTML/日志/临时基线 reader 留在 studio/.local/integration/plotfile-global-overview-20261003；仅提交代码/测试/处理后的指标。
