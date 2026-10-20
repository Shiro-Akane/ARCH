# 3C 活跃 Run 跨生产 Host 关闭／重开生命周期

执行源码：d3ccc711188e5d05342bd54426236162f97a0998，
分支 studio/compute-optim-integration，执行前干净。日期 2026-10-03。
这是联合 handoff 第4.2节要求的受控小型 CPU 生命周期工程检查；
不是原生 Electron close 按钮或用户可见窗口/终端验收。

## 生产路径与结果

直接启动现有 studio/host/desktop.ts（Node24生产入口），
使用独立所有权 token、随机绑定127.0.0.1端口、正式项目/配置/binary。
仅本机脚本持有 token，不打印或提交。没有 mock terminal 或替代实现。
本轮独立 Host 没有建立 Preview 会话；未触碰现有 Electron14246/
Host14292/warm14445，这三者执行后仍保留原 PID/startTicks。

有效固定网格 Sod 4096 cells、CFL0.4、tmax0.2、CPU28线程；
从先前有效输入仅改唯一 out_dir，log_dir 由 Core 沿现有规则解析。
未改变科学定义/阈值，无 build/configure。
重新 prepare通过，确认保存输入与 compiled binary，然后真实独立
xterm Run：e475ea66-60cf-4b9d-88d0-98415cb9579c。

首次生产 Host17389 正式关闭 stdin并 exit0，随后不存在。
关闭前 worker17413/startTicks417743、Core17425/startTicks417750
及 terminal17412/startTicks417741 均真实存在。
Host退出后这三者保持启动身份，Core仍为 R，worker为 S。
终端父进程从17389变为17384，这是关闭后的重托管，不是误杀。
控制台从1658 bytes增长到13970 bytes，证明不只是残留 state 文件；
计算仍推进。原始日志完整保留本机，不提交。

重开生产 Host17453，新的 project session。
/api/runs 找到相同 Run ID/PID/startTicks，状态 running；
经该Host所属项目历史记录提交 Stop，返回 stopRequested=true。
最终 stopped/SIGTERM，开始04:45:35.131Z、结束04:45:38.190Z。
worker/Core均消失；重开Host随后通过stdin关闭 exit0且不存在。
独立 terminal按-hold设计保留，已完成的 worker/Core没有遗留。

冻结 input.par 与保存 .par逐字节一致，输入SHA
94a77fc57ce8ce59ebebdf8820ee2df1375efddfd2b9f12218989c219a5f7a29。
binary SHA e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7，
size7177152；完整依赖 freshness仍unknown，不称Current源码binary。
原始Sod.par SHA保持9c8ba5b67718bf3bde6a14c447a5bfe442015813461b9a7ea8417a3e9ec79843。
原始H5/checkpoint/log、完整输入在摘要指定的ignored持久目录。

## 显示问题与未覆盖

只读WSLg排查：版本1.0.73.2，Weston登记单个2560×1440输出，
workarea2560×1392；Electron和桌面Explorer/Codex/msrdc处于同一
Windows Session1，日志有Electron窗口关联事件。
这些证据不足以证明用户可见性或根因。日志中的runtime-dir权限警告
没有被直接认定为故障原因，未修改WSLg权限/配置或再次重启。

用户仍报告任务栏图标而无页面；新窗口激活/最大化的人工回答未到。
工具捕获不能代替用户验收。原生close时活跃Run/Preview/AMR矩阵、
原生Stop按钮、其余文件取消/覆盖冲突仍未完成。
本次只证明生产Host关闭与恢复生命周期，不宣称整体3C/O7完成。
架构审计迁移及历史G批准待处理，不推进全模型/JENS/RZ。
仅文档/处理后身份摘要提交，diff check；不重复 unchanged229-suite。
没有源码改动、原始数据上传、push、tag、main merge或Windows适配。
