# Plotfile 隔离与 Project 切片接线

基线 54747a7ec3b5621f2ca681b356eb997cdb96e0cc。readPlotfileFieldSliceIsolated 复用原固定 reader
worker；Host 固定 Node/worker argv、shell=false，无客户端 program/env/cwd。
pure request module 在Host检查并复制field/block/start/count，不初始化HDF5。
metadata与slice共享单任务容量，取消/超时/超限时终止worker，child close后才释放。
15秒wall-clock上限、64KiB stdout和256MiB Node heap上限保持；
heap上限不是硬RSS/WASM cap，超响应明确失败，不截断成功。
Host验证slice字段/形状/值编码/坐标/原始索引与请求一致。

## Project与文件版本

readProjectPlotfileFieldSlice只接受projectId、relativePath、
expectedFileSha256和slice。复用checkedPath/projectRoot，拒绝跨项目、
绝对路径、traversal和symlink；进入await前复制身份与selection。
worker使用pinned descriptor，返回后还要求选定路径的device/inode/size/
mtime/ctime一致，SHA必须匹配之前metadata记录。
因此旧metadata不能与已更新文件的slice冒充同一结果。
该file身份不是case/config/build/EOS科学身份。

## 验证

真实维护Sod H5的隔离8样本读取及原始文件不变通过；
slice与metadata跨操作BUSY、取消、1ms超时后重新读取通过。
真实HDF5超大header使slice响应超过64KiB，worker明确OUTPUT_LIMIT，
随后metadata能重新取得容量；不会绕过响应预算。
Project正确读取、错误hash、跨项目/越界/symlink、额外执行字段以及
异步调用方修改path/start测试通过。

最终完整Studio/Host269/269、typecheck、lint、production build、
git diff --check通过。既有bundle warning保留。
测试日志仅存本机ignored studio/.local/integration/plotfile-isolated-slice-20261003/。

## 未完成范围

尚无production HTTP endpoint、正式Viewer/LOD/原值Inspector接入。
completion unknown、科学身份/单位/native cell geometry不可用，
renderEligible=false保持。下一步是正式IO contract与身份确认下的Host/API/UI接线，
不把本次reader基础验收当成Plotfile完整交付。
没有修改scientific Core/writer、运行simulation、ARCH build/CUDA、
上传raw、push/tag/main merge。
