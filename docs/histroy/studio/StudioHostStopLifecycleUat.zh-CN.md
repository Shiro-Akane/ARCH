# 3C Host/Core 显式 Stop 生命周期检查

日期：2026-10-03。执行前源码 6b2caa344180df4216ccb30818133feea0efefe9，
studio/compute-optim-integration，工作树干净。范围按
StudioConfigurationHandoff.zh-CN.md 4.2 的小型有效算例生命周期要求。

## 方法

使用当前 Electron 所拥有的认证 Local Host 与生产资产代理、现有受控
prepare/confirm/start/stop API；真实 xterm、worker 和 Core，没有模拟 hook，
没有开放任意命令/argv/env。已保存且原生 Open 关联的 StopSod4096.par
通过 canConfirm，明确确认保存输入与 compiled binary。
Core running 后以 /proc PID/startTicks/进程组确认身份，向所属 Run ID 请求 Stop。
没有按名称批量 kill。

第一次原生 1024-cell Run 9192f783-0b78-48b2-831a-3be839482921
在 1.743 秒自然结束，exit0、step2252、t0.2；未实际 Stop，不计 Stop PASS。
第二输入使用 4096 cells/256 root blocks/max_blocks512，
保持 Sod 物理、数值控制、CFL0.4、tmax0.2，仅增加观测余量。
这不是 benchmark、网格收敛或科学精度检查。

## 结果与身份

Run 6c0d8ba8-ec35-44ca-83c1-a1e626264a20：
终端16715、worker16716/startTicks391761、Core16728/startTicks391769，
Core 进程组16728，运行中 /proc 状态 R。
Stop 返回 stopRequested=true；最终 stopped、SIGTERM、exitCode null。
04:40:54.263Z 开始，04:40:54.510Z 结束。worker/Core 随后不存在。
终端按 -hold 保留，不称作 orphan worker。

Electron14246、Host14292、warm Preview14445 的 PID/startTicks/父进程/
进程组前后相同；Host 调度状态从 R 到 S 是正常变化，不是身份变化。
本次 Stop 未误杀它们。
冻结 input.par 与保存输入逐字节一致，SHA
1ab5c596b7f6d2225fceebdc06847c3d66a5b40610f1c30d87f8f57059404cef。
binary SHA e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7，
7177152 bytes；只声明 compiled-version，完整依赖 freshness 仍 unknown。
原始 simulation/Sod/Sod.par SHA 不变：
9c8ba5b67718bf3bde6a14c447a5bfe442015813461b9a7ea8417a3e9ec79843。

实际控制台记录 Setup、CPU max threads28、4096 cells、t=0 plt/checkpoint
和一个 timestep 后终止。中断轨迹不等于 t0.2 终点科学验收，
也不冒充指定的 t=0 AMR 科学对照。
原始 H5/checkpoint/输入/日志留在本机 studio/.local，不提交。

## 未覆盖

Host/Core Stop PASS；原生 Stop 按钮与用户可见独立窗口仍 NOT VERIFIED。
用户仍报告只有任务栏图标。工具捕获曾指向其他应用，重新定位后
可以捕获参数页，但截图不能代替人工可见性。已取消未启动的原生
Run 确认并最大化现有窗口，等待人工回答。

活跃 Run 跨 Studio/Host 关闭存活、active Preview/AMR 关闭、
其余文件取消/覆盖冲突矩阵仍未完成。不宣称整体3C/O7完成。
架构审计迁移和历史 G 输入批准仍待处理，不推进全模型/JENS/RZ。
仅增加文档/处理后摘要，执行 diff check；未重复既有通过的229-test
/lint/typecheck/build，无源码修改、Build、push、tag、main merge 或 Windows适配。
