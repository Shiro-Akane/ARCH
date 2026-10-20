# Project Plotfile只读审计UI进展

基线 5cd700445116868a73180109f4f7d170f2ae7e25，联合计划3C之后独立plt工作包。
ProjectPlotfileAudit复用当前Host/Project连接，输入project-relative文件路径，
读取metadata并选择单block bounded region。样本表显示原始global index、value及
存储Cartesian中心，不推断native cell bounds/volume/单位，也不生成假的热图层级。
最大512行，表格有界滚动；未接正式LOD/原生单元结果Renderer。

## 身份、取消和诚实表达

客户端响应预算128KiB（含Host封套），消费旧schema/protocol时明确失败。
严格匹配Project/path/file SHA、stored shape及slice字段/起点/shape；
global indices必须与选区逐项对应，不只检查仍在全文件范围内。
非有限raw字符串保留。completion unknown、units null、science identity null、
renderEligible false按当前audit contract校验；不能显示成科学认证。

Project更换或断开会unmount并abort；显式Cancel推进request sequence，
旧响应不能覆盖新读请求。失败/取消保留先前成功数据并显示原路径、field、
block/start/shape，未标为新请求成功。
这些保护实现与相关底层/客户端测试已通过，但真实原生race/cancel/failure桌面矩阵尚待UAT。
旧local-file1D入口保留，移除未经证实的completed/units-as-stored文案，
仍明确本地H5或Host project-relative路径需求。
不修改Working Copy、configRevision，不Save/Build/Run/Preview。

## 实际验证与未完成范围

客户端由真实维护Sod reader结果驱动，错误Project/file hash、伪造complete、
field单位/科学身份、raw长度/形状/错位索引均拒绝。
最终完整Studio/Host275/275、lint/typecheck/production build/diff PASS。
首次type-narrowing错误已修正；首次mode需求文案回归失败后补回仍有效localH5描述，
没有改弱测试。日志保存在本机ignored目录。

最终production Electron renderer实际切换Plotfile，输入仓库Sod路径，经真实Host
显示SHA/4blocks×16cells/time.15；PRES block2/start0/count1显示global32、
原值0.3054751636143017与x.5078125。未知完成/科学身份提示保留。
该检查是 **renderer/Host integration**，不是Computer Use/native Manual UAT，
不据此关闭完整桌面验收。两轮production尝试正常close exit0；
最终Electron 350218 /Host 350262均不存在，RT配置和binary SHA未变。
没有simulation/ARCH重编/CUDA、raw上传、push/tag/main merge。

正式科学Viewer、native-cell Inspector/LOD及Core IO契约仍未完成。
已向维护者请求完成发布、科学身份、单位/native bounds/volume的契约引用；
这不是已获批准或自动补入默认定义。全项目保持进行中。
原始诊断脚本/renderer数据/logs位于studio/.local/integration/plotfile-audit-ui-20261003/。
