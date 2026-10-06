# Plotfile 有界字段切片读取进展

基线 ae71df6df8481fab723b76271e972cf1951148b7。在已有 pinned descriptor/HDF5 结构检查/64MiB
文件预算与 streaming SHA 的同一读取事务中增加 readPlotfileFieldSlice。
不重新打开路径，不先读取整个 field/coordinates；读取后仍核对 size/mtime/ctime。
旧 inspectPlotfileMetadata 继续不载入字段数组。

单个 block 的 start/count 按 writer 存储轴顺序传入，最多512个样本；
values、Cartesian cell centers 和原文件 global linearIndices 对齐，x1最快。
数据只接受 float32/float64，不将大整数默默转换为浮点。
NaN/+Infinity/-Infinity 使用明确字符串编码及 NONFINITE_RAW_VALUES 诊断，
不能经过 JSON 默默变成 null/0。输入数组进入异步读取前复制。

## 验证

新增5项真实HDF5测试：二维非方形跨行切片、三维z/y/x与跨block索引、
NaN保留、非法field/block/bounds/shape后的恢复、512边界及超限前零hyperslab读取。
禁止 Dataset.value 的 getter 验证没有完整数组读路径。
原metadata/isolation/project身份检查与本次完整Studio/Host265/265、
lint、typecheck、production build、git diff --check全部通过。
既有bundle-size warning保留，未借机拆分或重构UI。
最初shell PATH赋值因宿主路径空格失败，随后用Python环境字典重新执行成功；
未绕过检查。

仓库真实Sod1D H5只读读取DENS前8样本，time=.15、全部有限、
文件SHA dadaba822af2ff977cf81a04927901485cd90b0d2502a97ef99f8e0cac50b5ed。
原始values/coordinates只保存本机ignored，提交摘要不包含数组。

## 交付边界

本次是正式Reader所需的有界原语，**不是完整Plotfile阶段交付**：
字段slice尚未接隔离worker/project endpoint/renderer/LOD/Inspector。
completion仍unknown、科学身份/单位/native cell geometry仍不可用，
renderEligible=false。下一步复用固定worker及project ownership接入slice，
保持取消/超时/输出预算；正式结果渲染仍需Core IO contract确认。
没有运行simulation、修改writer/scientific Core、重新编译ARCH、CUDA或push/tag/main merge。
原始证据位于studio/.local/integration/plotfile-slice-20261003/。
