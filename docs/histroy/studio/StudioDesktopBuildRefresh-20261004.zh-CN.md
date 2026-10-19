# Linux 生产桌面受管 Build 身份刷新（2026-10-04）

本次仅核对既有 Linux 桌面 Host 的 CPU Build 与初始化入口；不是完整项目验收。

## 来源与构建

- 清洁源码 HEAD：a28c6576bef02699cb71cda5121b89a613b1a7f0。
- Host-owned Profile：studio-cpu-release；source root：/home/arch/projects/ARCH-compute-optim。
- Build directory：/home/arch/projects/ARCH-compute-optim/build-studio-cpu；target：ARCH。
- 从真实 Linux 窗口点击 Build，未另起 Host、未主动 Configure、未替换归档 binary。
- Build ID：c99c8486-7cba-4fbc-beb7-22c849c5e1c7。开始 2026-10-03T15:31:59.012Z，结束 2026-10-03T15:32:32.041Z，约 33 秒。
- 原 binary SHA-256：ee3de6cf2b8cd54bc54e70a5c15ffdaa60a9fb39281ae243800f21d1286c2d5a。
- 新 binary SHA-256：1bf5a00d0b8b332d0379ff75785f888a5a892f5b8fffc3a03fae015654b2086e；实际文件哈希与 Manifest 一致。
- source Git dirty=false；显式 tracked inputs、CMake configuration inputs、compiler driver 前后稳定。
- Compiler dependency 输入前后稳定标记为 false，如实保留；不据此声称构建期间所有依赖稳定。Manifest 捕获 66 objects / 738 compiler files、173 link inputs、82 configuration inputs，link unavailable 为空。完整依赖 freshness 仍为 unknown。

## 原生桌面核对

Electron PID 839715，Host PID 839760；production asset index-CZj54lZc.js，未使用 Vite。

1. 构建前真实 Preview 因 tracked inputs changed 正确禁用。
2. Build succeeded 后需要显式 Refresh Project State 更新 session executable 指纹；仅刷新页面不足以立即恢复匹配。此交互限制保留，不将其写成无缝自动刷新。
3. Sod inspect initialization 返回 Current。项目刷新后现有自动预览调度生成真实 Preview，界面显示 Current。
4. Sod 密度显示 512 init samples、0.125–1 g/cm^3，x_pos=0.5。
5. 点击 Generate Initial AMR，显示 Current / Complete，8 个 L0 叶块、0 regrid passes。仅证明当前 0–0 级配置的根网格，未验证多层 AMR。
6. UI 明确显示 tracked inputs match successful build、complete dependency freshness unknown。

配置：studio/.local/integration/full-model-production-20261003/Sod.par；原始字节 SHA-256：9c8ba5b67718bf3bde6a14c447a5bfe442015813461b9a7ea8417a3e9ec79843。没有编辑、Save 或运行 simulation；本轮未生成科学 Plotfile/checkpoint。

## 证据与边界

完整 Manifest 留在 studio/.local/integration/desktop-build-refresh-20261004/post-build-manifest.json。该目录中 pre-build-manifest.json 在构建完成后才捕获，实际也是 post-build Manifest；前 binary 身份以 Manifest 内 preBuildBinaryFingerprint 为准，不能依赖文件名。

自动审批拒绝从进程参数解码会话 token 并发起请求；请求未执行，未通过替代路径提取凭据。后续使用已有授权的 Linux Computer Use 操作完成。

本轮不修改实现，因此不重复此前 316 项 Studio 回归或 Core baseline；git diff --check 单独检查新报告。此记录不关闭原生滚轮/平移验证、独立科学 oracle、JENS/RZ、CUDA、后续演化或总体目标。
