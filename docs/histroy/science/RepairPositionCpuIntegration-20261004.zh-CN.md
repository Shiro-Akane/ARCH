# Hydro repair 坐标修复：主 CPU 构建接入

## 实际构建身份

clean source commit：9fb34629981f434b8fc5d531adf44b3330c083c8。
现有 build-cpu / CPU Release，cmake --build --target ARCH --parallel 8。
5 步 Euler/RK2/RK3 dispatch 编译及 archive/link 成功，耗时约 19.97 s。

旧 ELF SHA-256：ca6437d283c23c8f94aa562b5678273b2c06e1c5df3cc79df38ebe40c7e2e069。
新 ELF SHA-256：e75300fc607f4f6078240e2a524c5b0d6085fd0d48b811369c6eaff0729ee8fc。
新 ELF size：7408664 bytes。

592 项保守源码/构建输入清单与前次 b38f44a8 构建逐项比对，
唯一变化为 src/driver/stages/DriverStages.h。
完整 fingerprint 本机保存；清单不等于 compiler/transitive dependency closure，
不能据此宣称任意 Studio Project Session 的 Manifest current。

## 定向验证

- shared_stage_scheduler / checkpoint_compatibility：2/2 PASS，0.42 s；
  对应目标已按当前源码构建。
- 当前 build-cpu 命令及 archive 重新链接实际 Driver/Runtime/IO repair fixture。
  四组 radial/axial coarse-fine × inner=0/1，各 5 个 mixed-AMR leaves。
- 每组产生 2560 个真实 repair stage events；代表坐标与独立原生 (r,0,z) 精确一致。
- 连续与 checkpoint 恢复后再推进的每组 10260 个状态/身份 word 一致，
  repair ledger、代表位置、controller 和来源 checkpoint SHA 保持。
- fixture SHA-256：6ffe45980d822bfc1582bffdf9917d16fafd61f19532144df58970347a022ae9。
  与前次当前 header fixture 相同也是记录结果，不声称产生新的科学参考。

现有 fixture 故意用诊断参数触发 repair；不是物理输入等效迁移，
没有调整生产 floor、科学定义、误差预算或验收阈值。
.001/step1 → .002/step2 是既有工程 unit 步骤，不是批准的科学演化终点。

## 范围与保留项

本次将已验证 header 修复纳入主 ARCH，不新增 Viewer/Run 功能。
未重新跑未变化的配置/Studio baseline，未新增科学场景。
公共 RZ gravity/regrid/CUDA 和科学验收仍未完成；O9 等待冻结输入与预算。
原始 H5/checkpoint/ELF/logs 留在 ignored studio/.local/integration/repair-position-main-cpu-20261004。
只提交此报告及处理后摘要；不 push/tag，不修改 root STATUS。
