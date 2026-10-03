# Plotfile 原生取消、超时与旧视口响应验证

实现基线：584508ea1d9fc23009da69973de7b13abf2fc00e，
分支 studio/compute-optim-integration；尚未 push。
Owner contract：23ff77c4f08419de2b3c5eadee214da2af25784e，已从准确 Git 对象完整核对，
不 merge main 或 owner 分支。

## 实际窗口证据

Linux production window 4393350，受管 clean source 64b0ce2f8d97553f59024978618f1e4848a974e1。
CPU ELF d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75。
这个 ELF 不是 H5 producer；既有真实 CellularDet 文件的 producer ELF 仍为
f82bb7ff16c4acf54ae84970b9b403dce3d0370a468d241169519b3bfd1f6a44。

只读加载既有 H5，20 活动叶块 × 16×16 = 5120 单元，
总览返回 32×24 像素，x1=[0,25.6]、x2=[0,12.8] cm。
20/20 native block outlines、L1/L2，与显示 LOD 分开。
读取期间 sticky path/status/Cancel 均可达。

1. 首次固定 reader SIGSTOP 后，手动观察/恢复耗时超过生产 15 秒预算。
   显示 Metadata read exceeded its wall-clock budget；保留旧 LOD，worker 最终退出。
   此项为超时保留证据，不算视口竞态通过。
2. 第二次实际 finer request 的固定 reader 暂停后，在窗口点击 Cancel。
   显示 Read cancelled; previous successful data retained；旧图仍在。
   已记录 PID/startTicks 最终消失，下一次 finer request 成功：
   Candidate viewport LOD loaded; full-domain display retained for Fit。
   未测回收延迟，也没有直接记录 Host 的最终 CANCELLED/TIMEOUT 分类，
   因此不把进程最终消失独立当作具体 kill 原因的证明。
   现有自动 live HTTP disconnect 的精确回收证据仍见 StickyCancel Summary。
3. 第三次固定 reader 暂停 8 秒后自动恢复，未改变生产 15 秒预算。
   请求前范围 x1=[2.56,23.04]、x2=[1.28,11.52]；
   窗口 Zoom in 后为 x1=[4.608,20.992]、x2=[2.304,10.496]。
   成功响应返回后显示 Viewport changed during read; result discarded, previous display retained。
   旧成功图保留，迟到数据不覆盖当前视口；Fit 恢复完整 domain。

worker 必须同时匹配自己的 Host parent PID、Node exe、startTicks，
以及固定 argv1 heap flag/argv2 plotfileMetadataWorker.ts。
只读取这三个固定 argv 前缀，不读取剩余请求参数、Host token 或进程环境。
每次暂停具备最终恢复保障；不控制其他 ARCH/Node 或用户终端。

## 清理与范围

正常关闭，launcher exit 0；8 个原 owned identities 加 3 个 reader identities 全部消失。
真实 H5 SHA 8cc5e9e1da007bdc2854a091517a487b08f40976ec9a5580c44f76a1f7d5550b，
测试 Sod.par SHA 9c8ba5b67718bf3bde6a14c447a5bfe442015813461b9a7ea8417a3e9ec79843，
与复制来源字节一致；clean build worktree 仍 clean。
startup Init 曾 preparing/current，未手动 Generate；不能声称本轮完全没有 Preview activity。
没有 Core Build、simulation、科学逻辑或数组修改。
本轮只新增报告，不重跑未变化的 331/331、lint、typecheck、production build。

SIGSTOP 是受控延迟注入，不是自然磁盘慢的性能测量；
普通 native wheel/pan routing 未在本轮验收。
全部科学语义、完整来源和完整发布认证仍需 owner review。
固定像素只限制响应，不限制首次 whole-file digest / 全叶扫描。
原始 H5/plt/checkpoint、完整日志和本轮进程记录留本机 ignored .local。
详见同名 Summary.json。未 push/tag；联合目标仍未完成。
