# RZ checkpoint 几何身份保护（2026-10-04）

## 范围
基线 bae18a20152bd71114cc7601574f73443b64bdce。按联合计划增加独立几何身份，不切换公共 Grid cylindrical、不修改物理或自动转换历史输入。checkpoint format 仍为 6；config 版本不变。

## 实现
根属性 geometry_semantics_revision=1 与 geometry_chart 同时存在。
existing 保持当前几何含义；axisymmetric-rz 仅合法于 dim=2 / cylindrical，并定义轴 r,z、动量 r,z,phi、完整环体测度。未知版本、chart、部分属性均拒绝。
旧文件无两属性时只允许 existing。显式 RZ expected identity 不能接收旧文件；read_chk 在 live topology/state replacement 前检查。
新默认 writer 写 existing 身份；内部 HDF payload 可显式记录 RZ。公共 Driver RZ write/restart 接线仍待完整几何迁移，不能声称生产 RZ restart 完成。

## 验证
CPU target arch_checkpoint_compatibility；CTest checkpoint_compatibility 1/1 PASS（最终0.15秒，头内实现调整后确认）。
真实 HDF 读回：版本/部分身份/非法 chart 拒绝，旧 Cartesian v6 兼容；显式 RZ metadata round-trip；实际旧二维 cylindrical HDF -> read_chk(RZ expected) 拒绝，已有 live leaves/rho/controller 未改。
内部 RZ HDF fixture 是身份验证，不是 RZ 科学演化输出；未据此确认全域原生网格、angular momentum、gravity 或长时 restart。
未来版本 writer 输入在 truncate 前拒绝，原 checkpoint SHA 不变。
首次编译发现校验放置和类型可见性问题，修正后通过；独立轻量头避免 HDF writer 新增链接依赖。
原始测试 H5 保留本机 build-cpu 测试目录，不提交。无 simulation、CUDA、push/tag。

## 未完成
公共 RZ/config/API/IO 完整迁移、角动量 AMR 权威约定、有限环重力和独立误差预算、CPU 完整场景及 CUDA / 冻结长轨迹仍待执行或 Core 确认。
