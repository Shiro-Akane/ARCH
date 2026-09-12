# S2：跨块设备执行（开发快照）

日期：2026-09-13。父提交：2904e9c13eec8e04b3e68c948f213564630f509c。

## 已实现

- Hydro 清零、面通量、散度、源项和更新使用跨块二维 launch；共享原有 device 工作函数，没有复制物理公式。
- 物理边界按 phase 跨块合批；Host/标量接口保留完整前置校验和完成 token 契约。
- 同一方向的 AMR flux registration 仍在该方向 scratch 覆盖前依次提交，保持共享 surface 的贡献顺序。
- 单批最多 1024 块；使用全局 species workspace 的大组分路线按单块 wave 提交，避免工作区别名冲突。
- 所有 EOS 实例化入口沿用统一 Hydro policy 路由；没有删除 EOS/network TU。

## 短检查

- MSVC Debug Host 合同工程 `build/hpc-s1-local-20260912/host-build`：4/4 通过。
- `python -m unittest discover -s tests/tooling -p test_audit_architecture.py`：98/98 通过。
- `python tools/audit_architecture.py .`、`git diff --check`：退出码 0。
- CUDA 测试增加三种 slot、批量边界和 Hydro kernel 计数断言；所有 slot 显式初始化。**尚未 NVCC 编译或运行。**

## 未验收项

没有 GPU 正确性、性能、尾批、多维 AMR 或 sanitizer 通过证据。不声称端到端加速。后续需补足阶段计划原有验收；此提交只供协作补测，不创建验收标签。S3 将复用本阶段新增的块描述符缓冲。

交付固定分支为 `codex/hpc-cuda-optimization`，不更新任一 main。用户仓库同步仍受两个历史 LFS 大表缺失阻塞，详见 S1 记录；不得绕过完整性检查。
