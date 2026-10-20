# 显式视口 LOD 与全域保留（2026-10-03）

本项对应联合计划 plt 小范围实现；非 O7/O9 或完整 Viewer验收。

## 实现与边界

- overview 可选 viewport={x:[min,max],y:[min,max]}，严格允许键、finite/increasing ranges、深拷贝。1D的y是inactive spatial coordinate，必须[0,1]，不把field纵轴错当作x2。
- 响应domain为实际显示视口，globalDomain独立保留全文件bounds；nativeBlocks仍属于全文件，validator用globalDomain核对原生bounds，不裁切/伪造block。
- 几何和字段仍全扫描，display权重只计算与viewport相交的贡献。width/height输出上限不变。域外视口返回明确null空像素，不填充最近值。
- zoom/pan只是重绘；只有用户点击Read finer current viewport按钮才发Host请求，不改Config/Save/Preview。局部响应仍为display mean，raw Inspector走精确point查询。
- UI分别保留一个full overview和一个refined overview；Fit使用已有full数据，不发查询。field/file/project变更或新full请求会清除refined。
- 视图revision在操作时更新，晚到viewport响应若与请求revision不匹配则丢弃并保留之前显示。现有request sequence/cancel和digest保护保持。
- revision guard与Fit缓存路径已实现；其真实native窗口事件/race行为仍需UAT，不以源码检查或SSR宣称通过。
- 未建立spatial index或持久WASM worker缓存。最多保留两份有界响应，不保留原始完整array。文件仍64MiB audit，worker stdout64KiB、15s timeout，geometry临时slice512单元。

## 检查与真实数据

302项Studio/Host测试PASS，lint/typecheck/production build PASS。
制造HDF5验证跨block viewport按原生bounds正确加权、x1-fastest、完整globalDomain/block身份、deep-copy与非法输入拒绝、域外空像素。
真实既有t=0文件：
- Sod viewport x1=[0.35,0.55]，32×1；HTTP4775bytes。
- CellularDet viewport x1=[0,3.2],x2=[5,8]，32×24；HTTP24089bytes。
- 与全域响应保持相同fileSHA/nativeBlocks，全域扫描覆盖仍分别192/5120cell；原始文件不变。
- HTTP→client→SVG SSR通过，finer/Fit入口和Viewport LOD状态明确。没有simulation/Core重编/新H5。
- 独立query成本计数见Summary JSON；rchar是process逻辑reads（含其他读取），read_bytes=0属缓存条件，不能宣称cold benchmark或large-file scaling。视口小响应仍不等于小扫描。

## Native window观察与下一步

本轮读取并按computer-use技能，用sky恢复现有WSLg窗口724452。实际截图正确显示ARCH Studio—ARCH-compute-optim，保留RT界面、Saved、Previous preview/build changed提示。
旧Electron PID355883仍存在；未关闭窗口或重启Host/Core。一次Ctrl+R后立即观察的界面无可见变化，不能据此证明reload成功或新bundle已装载。
没有把旧RT窗口截图当作新Plotfile Viewer UAT。下一步须使用当前production assets/Host建立新Viewer会话，完成真实Linux zoom/pan/point/level/finer/Fit/cancel使用核对。

仍需spatial index/有界缓存、真实大型读取/RSS/取消、单位/测度/完整科学身份与显示语义owner review；不扩展curved/3D/XDMF或Windows适配。
原始H5/HTTP/HTML/完整日志留studio/.local/integration/plotfile-viewport-20261003；提交实现、测试和处理后的摘要。
