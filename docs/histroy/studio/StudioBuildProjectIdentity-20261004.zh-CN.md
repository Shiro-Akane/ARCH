# 受管 Build 后项目 binary 身份衔接修复

基线 ffcd32fd60ec574d0e2fc5cf7e75c54befe6cb27。上一轮真实 Linux 桌面 Build succeeded 后，项目 session 仍记录旧 executable 指纹，配置 inspection/Preview Profile 无法立即匹配；手动 Refresh Project State 后才恢复 Current。

## 最小实现

BuildProvider 复用已有 HttpLocalHostAdapter.refresh() 与 validateSnapshot，在成功的受管 Build 与当前项目所选 executable 指纹不一致时刷新项目身份。仅接受同 project ID、managed source root、output relative/absolute path 以及 manifest binary SHA 的响应。不根据文件名、当前 Git HEAD 或未知 freshness 推断正确性。

没有读写 Config Working Copy、执行 Save、主动配置/编译或更改 Preview 策略；现有自动预览调度在匹配后继续工作。没有把 freshness-unknown 改成完全 verified。

每个 build/旧指纹组合只请求一次。失败保持明确错误，后续 poll 不重复请求或静默清错；用户可用既有手动 Refresh Project State 检查恢复。项目切换、组件卸载或更新的 snapshot 会淘汰晚到刷新结果。拒绝不匹配的 refreshed binary，不让旧成功 Build 给外部变更背书。

## 验证

新增四个运行时回归：成功身份更新与重复 polling、active/failed/跨 project/source/output 的拒绝、异步晚到响应淘汰、offline/错误 binary 的失败保持。320/320 npm tests、lint、typecheck、build 与 diff check 通过。原 chunk size 警告仍保留，未为本补丁重构打包。

production asset index-CRygbPVh.js。在原 Linux Electron PID 839715 / Host PID 839760 中通过 Ctrl+R 加载，等待当前 binary registry/inspection 完成；未点击 Refresh Project State，Sod Preview 自动恢复 Current，密度与 x_pos 标记正常显示。此 UAT 是保存状态配置的 production reload；不声称重新执行了 Build 全周期、原生 dirty-config/项目切换测试或 AMR。异步防护由自动化测试补证。

binary SHA 1bf5a00d0b8b332d0379ff75785f888a5a892f5b8fffc3a03fae015654b2086e、配置 SHA 9c8ba5b67718bf3bde6a14c447a5bfe442015813461b9a7ea8417a3e9ec79843 前后不变。没有 Core 改动，因此不重跑不相关 Core baseline。本轮无 simulation、Plotfile、Configure、重复 CPU Build、CUDA、push 或 tag。

完整测试日志在本机 ignored studio/.local/integration/build-project-refresh-20261004，提交处理后的摘要。此补丁关闭上轮观察到的 session identity 手动刷新衔接问题，不关闭完整依赖 freshness、科学 review、JENS/RZ 和后续平台目标。
