# Plotfile 正式 HTTP / Linux renderer 链路节点（2026-10-05）

## 新增证据

PASS：补齐前两节点未挂接的生产读取链。基线 e8197850421c45d5e72a465ddc063a4e514966eb。
实际 LocalHostProvider / HttpLocalHostAdapter → ProjectPlotfileAudit →
生产 createHostServer / openProject / 项目路径校验 → isolated reader → client validator →
真实 Linux Electron 绘图事件 → 实际 PlotfileNativeInspector。
加入正式 styles.css 和 sample-page plotfile-page 容器，生产代码未修改。

Host 与静态资产都监听 127.0.0.1 的 OS 分配端口，项目为唯一现有工作区。
未选择 Build Profile/配置/binary；只读项目文件检查，不据此宣称 binary current 或完整工作流。
真实 native mouse 点击读取按钮、输入路径/索引、选择 DENS；
metadata / slice / overview / point 全部通过实际 HTTP，没有测试 IPC 代替该链路。

## 检查范围

同一旧 Sod 1D / CellularDet Cartesian 2D AMR 文件，不生成新 simulation 输出。
先读取 fine block 非方形 [2,3] 切片（1D [3]），滚轮实际改变宽度，
拖动实际平移且不发送 HTTP 数据请求，native 点击显示指定原始 DENS。
再读取 32×1 / 32×24 全域 LOD；缩放/拖动只重绘，不触发查询；
实际 LOD 点击坐标经正式 HTTP point query 搜索唯一原生叶单元。
这里直接检查真实鼠标映射坐标，不使用前节点 row→stored-center adapter。

独立 h5py 按文件 bounds、half-open/global-maximum-inclusive 规则重新查找：
Sod index 99 / level 2 / key 2/8/0/0；
CellularDet index 512 / level 1 / key 1/2/1/0。
DENS、中心坐标、bounds、measure FP64 位串一致；数组 i/j 顺序和 block identity 一致。
Inspector 显示该 raw 值、文件 SHA、logicalKey；不是 LOD 平均值。
请求总数为成功 200×9（project一次+两文件四种读取）、缺失文件 400×2，
另有真实 CORS OPTIONS。失败后此前 LOD / Inspector 保留。
文件 SHA 不变；原 run_id=unknown 不升级，producer ELF 保持文件实际1bdd71ed...。

## 诊断历史

初轮选择器跨层转义错误；早期探针缺失全局样式，不能作为正式页面验收。
长页面定位后向尚未稳定的 compositor 发送事件，曾得到无滚轮或异常拖动位移；
现统一 scrollIntoView 后等待，再移动实际指针并发送事件。
一次生成读取路径包含额外字符，Host如实400；未证实属于产品输入缺陷，
该失败 trace 保留，最终读取路径和SHA均与独立 oracle 相符。
所有失败不删除，不以成功重跑抹掉记录；报告不据此推断泛化的输入可靠性。
生产组件/科学阈值均未调整。

## 复现与清理

先按 export_renderer_point_oracle.py 对本机已有两文件生成新 oracle，再运行：

    node validation/io/verify_project_plotfile_http_renderer.mjs LOCAL_ORACLE.json NEW_LOCAL_DIRECTORY
    python validation/io/verify_http_renderer_point_oracle.py --project-root ARCH_ROOT --trace NEW_LOCAL_DIRECTORY/trace.json --output NEW_LOCAL_DIRECTORY/verified-summary.json

需 Linux 图形会话、Node/Electron/h5py。输出目录必须不存在。
窗口/Host/资产 server 属于探针；退出关闭，最终核对两监听端口拒绝连接。
原 trace/response/数组/文件路径/日志只在本机；提交两脚本、处理summary与此报告。
JS语法、独立 oracle、4项项目身份/路径回归和 diff check 通过，不重跑无改动的科学 baseline。

## 仍开放

这是实际生产组件/HTTP的自动工程检查，不是完整 App/desktop launcher 的人工 UAT。
只有两份既有 t=0 的 DENS，不覆盖二维演化、全字段/全像素、完整取消/竞态/发布身份、
独立 EOS/科学 oracle、首域扫描成本或新 binary 生产输出。
原 JENS/CUDA授权、RZ force/axis/viscosity finding、科学长包冻结及完整科学签收门槛不变。
