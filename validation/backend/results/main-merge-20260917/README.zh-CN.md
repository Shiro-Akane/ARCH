# 个人 main 合并验证

2026-09-17。这是 predictive-AMR 采集接口迁移的 CPU 集成检查，不是 CUDA 性能发布。
合并策略和历史保留边界见 [合并说明](../../../../docs/development/PersonalMainOptimizationMerge.zh-CN.md)。

## 实际通过

1. 新 `test_predictive_amr_recorder.cpp` 用 GCC 11 独立编译/运行通过：
   1D/2D/3D、默认关闭、0/1 与 true/false、配置拒绝、五类 schema v2 输出、
   canonical criterion 标签、只读字段、异常回滚和池复用。
   自定义指标回调只是 Host-lowered 合同检查，不冒充 GPU 执行。
2. 独立完整 CPU ARCH 编译/链接；`--parallel 1`，Debug 配置显式 `-O1 -g0`，
   strict FP、CPU-only、KLU 关闭；复用原固定 HighFive/HDF5 安装，不下载或升级库。
   这不是标准带符号 Debug 的内存资格，也不提供性能样本。
3. 5/5 CTest：predictive_amr_recorder、topology_transaction、amr_operation_plans、
   checkpoint_compatibility、runtime_probe_and_capabilities。
4. 实际应用 v2：15 次运行、9 组 checkpoint 比较。
   1D 使用维护中的 Sod，2D/3D 使用维护中的 Sedov；均显式固定参数和 OMP=1。
   旧已验证优化二进制对新关闭采集版本使用 `max(abs(a-b)/max(1,abs(a),abs(b))) <= 2e-10`；
   新二进制开关采集，以及连续 6 步对 3＋3 步 restart，要求所有 checkpoint 数据集逐值完全相等。
   检查真实退出码、完成步数、HDF schema/拓扑、有限数、五类记录文件及标签统计；没有速度结论。
5. 本机架构审计通过；100 项 `test_audit_architecture.py` 测试全部通过（12.105 秒）。

CPU 构建及测试 guard 未中止，采样 peak owned RSS 1,187,576 KiB，
min available 113,199,108 KiB，swap 31,528 KiB 前后未增，wall 224.226 秒。
这些是本次 114 GiB VM 的观察，不是 16 GiB WSL 验收。

## 源码与失败记录

实际构建源码来自 Git tree `723e51b302edae758293664f3c1d9d26a5218694` 的显式 source archive：
`source-v2.tar`（6,256,640 bytes），SHA-256
`24dcfae52cf22c0d1d5f78a82069cdf815b32ebdb4a284701f5f9a6344e52590`。
最终 Git 入库时将六个编辑过的源/构建文件 CRLF 规范为 LF；
`verify_merge.py` 逐文件核验编译输入与最终暂存树只能有行尾差异，不能有代码差异。
新增应用 runner 是后处理/调度工具，不是 ARCH 的编译输入。

- source-v1 的本机打包因不存在的 `cases` 路径拒绝，零字节产物未部署。
- 首次 host-check 在上传完成前触发 SHA 门槛拒绝，未执行编译；随后传输完成并核对 SHA 后运行。
- cpu-build-v1 的配置成功，但构建命令引用已退休的 `arch_checkpoint_restart` target，被 Ninja 拒绝；
  v2 仅纠正为现有目标，原命令/日志保留，没有修改源码绕过构建。
- application-v1 的 1D 三对照通过，2D 基线被现有 Sod 的 1D-only 约束拒绝。
  v2 独立目录改用现有 2D/3D Sedov，不放松 Sod 检查，不重写 v1。
- 首次本机架构审计因 sparse checkout 未展开三个 simulation 输入而失败；展开后审计通过，
  这些受保护文件没有修改。
- 首次 collector 将 `st_size` 属性写成函数调用，在写归档前失败；修正后另存 collector v2，
  原 v1 脚本保留，没有重跑数值计算。

## 未验证边界

本轮没有运行新合并代码的完整 CUDA archive、真实 GPU 采集开关/restart 或 sanitizer。
已有 S5 及 native-wave 记录保持原始身份；新窗口 factory 的 GPU focused 阶段仍待空闲窗口。
不要把合并、push、CPU 回归或源文件核验当作超大网络 CUDA 性能验收。

完整 logs、应用 report、输入、构建命令和回执在本目录 records 下。
raw 包含本轮 HDF、实际源码 tar 和二进制，共 329 个成员；compact 投影为 254 个成员。
本次 raw 仅 3,435,564 bytes，除服务器与本机副本外，也完整提交到
[archives/main-merge-raw-v1.tar.gz](archives/main-merge-raw-v1.tar.gz)。
SHA-256：`fbbb1a74e4065b738d0d4e8e0df92e46673e291dccef890bdc614e61facf6a75`。
compact SHA-256：`2ce8d9d5b47e63e6176ecb5da8d899dc2bc01aa4a3501ecf5ab9b7f9b5c5e385`。
两个包及所有成员均通过独立本机字节核验，再投影到 records；回执随 Git 保存。
