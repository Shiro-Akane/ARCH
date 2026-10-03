# Plotfile 只读查询成本记录（2026-10-03）

本项对应联合计划中 3C 之后的独立 plt 排期，为全域 Viewer/LOD 的资源边界提供实测基线。它不代表 Viewer 已完成，也不替代 O7/O9 科学验收。

测量基线：b0d2d93daaef08b1f5e41b510801061218db77f3；Linux/WSL、Node v24.21.0。未修改 reader、科学 Core 或原始文件。每个查询在独立 Node 进程中执行，使用既有 t=0 Sod/CellularDet 输出。

## 可重复入口

node validation/io/measure_plotfile_query.mjs PROJECT_ROOT PLOTFILE [SLICE_JSON]

示例 slice JSON：{"field":"DENS","block":0,"start":[0,0],"count":[1,3]}。1D start/count 各一个元素。文件仍受现有 64 MiB audit 上限、slice 512 样本上限约束；本工具不扩张读取授权。

## 实测（每项一次，不作速度排名）

| Case / 查询 | 文件 bytes | 返回 bytes | 逻辑读取 bytes | 存储读取 bytes | wall ms | 进程峰值 RSS KiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Sod / metadata | 38272 | 1865 | 64754 | 0 | 85.07 | 112872 |
| Sod / slice | 38272 | 2676 | 66178 | 0 | 78.75 | 113276 |
| CellularDet / metadata | 472888 | 2058 | 499642 | 0 | 75.54 | 113840 |
| CellularDet / slice | 472888 | 3058 | 522186 | 0 | 79.41 | 113684 |

## 计数语义与发现

- 每次 metadata/slice 都完整扫描文件以计算 digest。Cellular 仅返回 2058/3058 bytes，仍读出至少 472888 bytes 的 digest 输入。小响应并不等于小读取。
- rchar 是查询期间整个进程的逻辑 read 系统调用字节，含非 HDF5 读取；不能称为纯 dataset 读取量。
- read_bytes 是 Linux 对该进程记账的存储读取量，本次全为 0，与缓存读取相符；不表示没有逻辑读取。未 drop caches，不声称冷缓存结果。
- peak RSS 是进程生命周期峰值，含 Node/import/HDF5 WASM；不是返回数组的增量内存。当前四次约 110–111 MiB，不能外推到大型全域总览。
- HDF5 单 dataset 的实际读取字节目前没有独立 instrumentation，报告为 null。首次全域总览即使使用固定像素输出，仍可能扫描大量叶块。
- 未改变缓存策略或取消文件身份校验。后续候选 index/LOD/cache 必须保持文件身份、原生数组与显示聚合分离，另测实际大型读取、资源上限和取消行为。

## 检查与本地保留

四次真实查询、返回上限、完整 digest 计数、读后原始 SHA 不变、Node 语法、缺失参数拒绝均 PASS。无 simulation、无新 scientific output，无 GUI 操作。仅提交工具和处理后的指标；原始 H5、stdout/stderr 位于 studio/.local/integration/plotfile-query-cost-20261003。

下一交付仍是首版全域/局部显示、zoom/pan、Native AMR outlines 与 Inspector 联动；本次成本基线不作为该交付完成证据。
