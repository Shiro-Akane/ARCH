# Plotfile 精确物理坐标回查（2026-10-03）

本项属于联合计划 plt 小范围贯通，不代表 O7/O9、完整 Viewer 或 Linux native UAT 完成。

## 实现

- POST /api/plotfile/audit-point；唯一输入是项目/file/digest身份与 pointQuery={field,point:[x1[,x2]]}，拒绝额外 program/env/command、非有限坐标与维度不匹配。
- 只支持已记录候选 NativeGrid 的 Cartesian 1D/2D。不用文件名、当前 Config 或字段值推断 cell。
- 两遍最多512单元片段扫描 native bounds：先求全域边界，再找匹配的存盘cell。规则明确为 half-open; global-maximum-inclusive，无abs/epsilon/nearest/interpolation。
- 仅一个匹配时通过原有 readSlice 回读1个cell的真实field、center、bounds、measure、level/key。返回 audit-point-1 和 pointEvidence（坐标、规则、scannedCells、matchCount=1、domain）。
- NO_NATIVE_CELL 或 AMBIGUOUS_NATIVE_CELL 失败，不猜更细层/最近样本。保持现有worker错误映射，域外HTTP为422并明确NO_NATIVE_CELL；stale digest拒绝为400。
- 沿用单owned worker、15s timeout、64KiB stdout、项目范围/pinned FD/完整hash/读后身份检查、断开取消/reap后释放capacity。没有任意browser execution。
- client 同时校验完整原生slice身份、pointEvidence与目标cell bounds。Inspector显示Queried physical point及搜索覆盖，不把LOD mean或键盘representative当成精确鼠标值。
- 全域SVG鼠标up将同一viewport映射的物理坐标提交点查询。键盘pixel选择仍为largest-overlap representative，UI文字明确区分。Zoom/pan/Fit依旧只是显示，不发请求、不改Config/Save/Preview。
- 请求位置的LOD高亮不等于成功返回的Inspector；Inspector一直保留最后成功结果，失败/取消不替换。全窗口交互语义仍待Linux UAT核对。
- 原始H5、科学Core/数组/precision、writer、checkpoint无修改。

## 验证

300项Studio/Host测试 PASS；lint、typecheck、production build PASS。制造数据检查shared cell/block boundary、global maximum、非方形x1-fastest、空匹配、维度错、overlap拒绝、cancel和project/digest/body保护。
真实已有t=0 H5经受控HTTP/client/Inspector SSR：
- Sod：point=[0.501953125]→global index96→raw DENS0.125，HTTP2898bytes。
- CellularDet：point=[0.5,6.5]→global index3074→raw DENS43375362.843074，HTTP3150bytes。
- 与已有原生slice索引/值完全一致，pointEvidence分别记录192/5120扫描覆盖；域外与旧digest拒绝，源H5读取前后不变。
- SSR/纯映射不是实际Linux窗口鼠标验收。

## 成本与限制

独立Node进程、缓存条件实测：
- Sod：返回2723bytes，process rchar79442bytes，peakRSS115464KiB。
- CellularDet：返回2967bytes，process rchar2482058bytes，peakRSS123396KiB。
- 仍扫描全域bounds并完整digest，没有spatial index/缓存复用。小响应不代表小读取；rchar含非HDF5读，read_bytes本次为0，不声称冷读取性能或大型文件支持。
- 当前仍64MiB audit限制；view finer query/index/有界cache、真实大型RSS/取消、native窗口UAT、单位/测度/科学身份review未完成。
- 浮点共享边界若在存盘bounds实际重叠，可能明确拒绝；不额外放宽坐标容差以隐藏finding，交owner review。
- 原始H5、HTTP响应/HTML/日志留在studio/.local/integration/plotfile-native-point-20261003。提交只有实现/测试/处理指标，无simulation/CUDA/Windows适配。
