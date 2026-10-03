# 全模型 Host 接入进度

2026-10-03；实施基线 9164222c4091c629e9d6ea5fb170463cf6f44b60。

## 本次实现

Host 从当前成功 Build 对应 binary 的真实 modelCapabilities 生成 case/dimension Profiles，
按 build ID 与 binary SHA 缓存能力查询；浏览器仍不能提供 case 命令、program、args、cwd 或 env。
支持三轴请求与 volume 响应的有界校验，使用解析网格维度及 native coordinate metadata。
已有 Sod/Cellular Profiles 保持兼容。新增 input 阶段校验。

能力查询纳入 Host 所有权，关闭取消并等待实际 child close。
故意忽略 SIGTERM 的测试进程必须强制终止；初次关闭测试由人工释放，不能计作通过。
修正后独立重跑通过，关闭耗时低于1秒。不可执行 binary 在能力阶段拒绝，无活动请求。

## 验证

最终 Studio/Host 237/237、lint、typecheck、production build、git diff --check PASS。
全回归首次失败为配置检查 mock 缺少 capabilities 命令，补全 fixture 后最终通过。
日志与失败原始证据保留 studio/.local/integration/full-model-host-20261003。
实际 build-cpu/bin/ARCH --preview-capabilities 被相同 TS validators/Profiles 工厂接受：
14 模型、22 case/dimension Profiles。

本次查询没有 Setup/Init、simulation 或 scientific output。
这里只证明实际能力数据可被 Host 校验与转换，不能证明真实 Host 请求/字段显示已通过。

## 尚未完成

配置 inspection 与实际维度选择、通用 provider、三维 display-only 切片、
uniform-state 视图、曲线坐标显示、AMR 3D/曲线校验与视图、生产 binary/真实 Manifest 更新、
真实 Host 多模型请求与桌面 UAT 仍待实施。独立 plt、JENS、RZ、CUDA 与科学验收未完成。
完整依赖 freshness 仍不得宣称 current；生产 UI binary 本次没有替换。
不发布 tag、不 push、不合并 main；本报告不是阶段完成或科学认证。
