# 活跃 Run、Preview 取消与不同项目重开隔离验证

2026-10-03；源码基线 9a5c8958bbc4cda738c0abebf182bc698222f0d3，开始/结束两项目工作树 clean。

**PASS：生产 desktop Host 的同一场景验证。** 本次未操作 Electron 原生 project picker；
原生窗口关闭的直接证据继续引用 StudioNativeActiveRunCloseUat.zh-CN.md，
不将本次 HTTP Host 调用写成桌面点击 UAT。

## 输入及实际操作

复用 OriginalSod.backup.txt 的既有 CPU 输入：256 blocks/4096 cells，tmax=0.2、cfl=0.4、
HLLC/MUSCL/RK2、IdealGas、无引力/燃烧/扩散。仅替换 out_dir 为独立本机目录。
本次不是280步的另一份小网格续算输入；实际步数8994，未放大网格或终点以延长验证。

以真实 studio/host/desktop.ts、Host-owned studio-cpu-release 建立独立本地 Host，
先成功完成512点 CPU Sod Init Preview。核对该 Host 的唯一直属 --preview-session PID/start identity，
在idle warm worker上SIGSTOP，并由独立90秒PID/start/state guard保护；没有暂停 Run/Core simulation。
因此取消覆盖的是等待自有 worker 的真实请求，不声称自然 Init 数学调用中途取消。

使用真实 /api/run/prepare、保存字节 SHA、显式 compiled-version confirmation 交付 Run。
保持同一 Run 活跃，启动并观察 generating/stage=request，取消该 Preview。
关闭第一 Host stdin并获得exit0，再以同一生产 Host入口打开 ARCH-mainline 的现有固定CPU profile。
新项目具有不同 projectId；仅注册发现与关联，未在第二项目 Preview/Run/Configure/Build。

## 直接证据

Run 740d4d6c-6cba-41ce-b7df-d6a8a7585eaf：
worker PID124990/start1238599；ARCH PID125008/start1238607。
取消前、取消后、原Host关闭后、另一项目打开后均为同一running identity。

| 观察点 | console.log字节数 |
| --- | ---: |
| Preview开始前 | 1688 |
| Preview取消后 | 27122 |
| 原Host关闭后 | 42134 |
| 另一项目打开后 | 73562 |

两Host PID124688/125157均exit0，自有Preview worker已退出。
Run于07:13:01.196Z开始，07:13:31.867Z自然succeeded/exit0；
日志确认Final Time=0.2、Total Steps=8994，未由Stop或缩小终点结束。
Run worker/Core退出后，仅关闭runId匹配、启动身份复核的已完成自有xterm；
当前原生Studio窗口及其Host未操作。

源输入、测试保存字节和两项目binary SHA/size/mtime前后不变；两项目HEAD/dirty一致。
当前binary e506619f...03813bc7，另一项目binary fb4f9de2...c2b5d9a4。
仍使用compiled-version-only语义，Build完整link依赖不足时保持freshness unknown。

## 覆盖边界与本机数据

此结果关闭3C工程出口最后一项直接隔离证据缺口，不证明其他case/backend科学演化正确。
无源码改动、Configure/Build/CUDA、push/tag/main merge；没有重新运行未变化的235项回归。
tmax=0.2 simulation是既有明确授权的3C小型CPU工作流验证；Preview仍不进入simulation。

本机记录：studio/.local/integration/run-preview-project-isolation-550d3aff-ded7-4a35-b213-5173a673c20f，
包含verify.py、before/events/summary、输入、完整日志及输出；
持久Run记录位于studio/.local/runs/740d4d6c-6cba-41ce-b7df-d6a8a7585eaf。
H5/plt/checkpoint/完整数组不提交，不对其宣称独立科学误差验收。
精简身份和观测详见StudioRunPreviewProjectIsolation.Summary.json。
