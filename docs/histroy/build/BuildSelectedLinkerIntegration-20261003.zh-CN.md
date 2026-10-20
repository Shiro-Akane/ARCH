# 真实增量 Build 与选定链接器 Manifest 重载

2026-10-03，Linux/WSL。源码提交 164de218ae7c9797f4ad5d4e0f6c17f9d501ef3a，构建时工作树干净。

标准 Host-owned CPU Build 成功，Build ID 39eb4f9e-3618-4011-8810-4880e473f6ff。
Ninja 实际输出 Re-checking globbed directories / no work to do；没有执行新的编译或链接。因此本次证明正常增量 Build 的证据采集、持久化和重载，不证明干净重建或本次实际执行 mold。

Manifest：
- CMake 输入 82 项，compiler inputs 735 项，link inputs 172 项，缺失 link inputs 0。
- configurationInputsStableDuringBuild、compilerInputsStableDuringBuild、compilerDriversStableDuringBuild 均 true。
- selected-ld.mold 路径 /usr/bin/ld.mold，解析 /usr/bin/mold，SHA-256 9e23dde239d96e75691c107d0f387039aae7698ca73defec6080ada0eb920723，19054816 bytes。
- binary SHA-256 ee3de6cf2b8cd54bc54e70a5c15ffdaa60a9fb39281ae243800f21d1286c2d5a，与构建前相同。

同进程新 Runner 及独立 Node PID 308659 重载同一 build ID，changedInputs=[]，状态 freshness-unknown，原因 Tracked inputs match; full dependency coverage is unknown。
验证 Node 308416 已退出；独立重载进程正常退出。未启动 UI、Preview 或 simulation，无新的科学输出。

前置 164de218 的 260/260 Studio/Host 回归、lint、typecheck/production build 继续适用；本次仅取证，不重复无变化检查。
完整本地日志和 Manifest 留在 studio/.local/integration/linker-manifest-real-build-20261003/；提交精简 Summary，不上传原始数据。

已关闭本轮选定链接器身份接入的真实增量验证项。全依赖覆盖、全模型桌面完整矩阵、正式 plt、JENS/RZ、CUDA 和科学性能验收仍未完成；不以此宣布全项目完成。
