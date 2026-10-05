# Plotfile 真实文件 Linux renderer / Inspector 读回节点（2026-10-05）

## 结果

工程 PASS；两份既有 t=0 文件，各一个 DENS 细化块样本。
基线 ebacc4f698316c103daefb34340f3f933d6746aa。
Sod：level 3、block 8、stored index 129、logicalKey 3/12/0/0；
CellularDet Cartesian：level 2、block 4、index 1041、logicalKey 2/0/0/0。
二维读取切片 shape=[2,3]、x1-fastest，不是整块或整域验收。
保留原始文件 producer ELF 1bdd71ed344f01dd9722898479d37780e81a834f4d0bdea501b23a933804e1a8；
run_id 的原始 unknown 不升级为当前身份。不要用当前 CPU ELF/SHA 替换。

## 链路及独立参考

独立 h5py 从原 HDF5 导出 raw FP64 值、measure 位串、bounds、索引、level/key。
生产隔离 slice reader + client validator 提供真实 payload；
Vite 打包实际 PlotfileNativeView 和 PlotfileNativeInspector，
Linux Electron 44.4.3 / Chromium 152.0.7977.130 软件渲染，真实发送 wheel/mouse 事件。

滚轮实际改变图形宽度，拖动实际平移且不选择；然后点击真实绘制单元。
NativeView 选择 row，再以该 row 的存储中心坐标，经探针固定 IPC adapter 调用
生产隔离 point reader 与 client validator，最后挂接实际 Inspector。
核对 native index、DENS/measure FP64 位串、bounds、level/logicalKey，
以及 Inspector 的 raw value、文件 SHA、logicalKey 文本。
原 response 不变，Fit 在 SVG 本地坐标恢复。两份 HDF5 SHA 前后不变。
这不是直接使用原鼠标坐标查询：row→stored-center 的边界在摘要中明确保留。

## 失败与更正

首轮生成 child 脚本转义错误；Electron 错误退出0不能作为通过。
探针已增加“正式 summary 必须存在、status PASS 且条目数匹配”检查，
首轮无 summary，所有初步 stdout PASS 无效，失败资产本机保留。
第二轮 Fit 使用绝对屏幕像素比较，在 Inspector 展开后宽度变化而失败；
改为独立 SVG inverse CTM 本地坐标，区分 viewport 布局与物理映射。
没有修改产品或放宽科学阈值。最终轮才是本报告 PASS。

## 复现

    python validation/io/export_renderer_point_oracle.py --plot EXISTING_SOD_H5 --plot EXISTING_CELLULAR_H5 --output NEW_LOCAL_ORACLE.json
    node validation/io/verify_plotfile_renderer_readback.mjs NEW_LOCAL_ORACLE.json NEW_LOCAL_OUTPUT_DIRECTORY

目录/文件不得已存在，需 Linux 图形会话和项目 Node/Electron/h5py 依赖。
导出脚本重新运行，oracle 与首次独立导出完全一致；JS 语法、Python AST、
diff check 通过。无需重复无改动的科学 baseline。
只提交两份脚本、本报告及处理 summary；HDF5、路径 manifest、response 数组、
生成资产、完整错误全部保留本机 studio/.local/integration/plotfile-real-renderer-readback-20261005。

## 尚未签收

自定义测试 IPC 非生产 HTTP/Host/ProjectPlotfileAudit；没有完整应用入口、全域 LOD 点击、
多字段多时刻/二维演化、并发/取消/过期链、科学 oracle 或人工 UAT 的本次覆盖。
既有证据不能相加自动宣称端到端验收。首版真实数据/缩放/Inspector 部件链有新增直接证据，
完整生产应用 UAT 和来源完整性科学 review 继续开放。
科学长包、JENS CUDA 公共入口授权、RZ force/axis/viscosity 门槛保持不变。
