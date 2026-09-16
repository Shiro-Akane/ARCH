# cuDSS 性能方向的版本与约束核对

2026-09-17。通过 agent-reach 的 Exa 搜索与 Jina 网页读取核对 NVIDIA 官方资料；
没有安装／升级库，没有运行新服务器实验，也没有修改当前数值队列。
尝试的 `/0.8.0/` 独立网页路径返回 404，不能作为版本证据；以下使用当前官方页及
其中明确标注的 v0.8.0 release notes。服务器冻结版本仍为 0.8.0.10。

## 已确认的限制

- v0.8.0 加入了 uniform batch 的选择掩码，并记录了 uniform factorization 的性能改进。
  这不等于所有排序模式都支持该功能。[官方版本记录](https://docs.nvidia.com/cuda/cudss/release_notes.html#cudss-v0-8-0)
- `UBATCH_SIZE`、`UBATCH_INDEX` 和 `UBATCH_MASK` 的文档均列出：不支持
  `BTF_COLAMD`／`COLAMD`。因此不能把掩码直接应用于当前冻结 provider，
  也不应默默更换排序来绕过限制。[参数说明](https://docs.nvidia.com/cuda/cudss/types.html#c.cudssConfigParam_t.CUDSS_CONFIG_UBATCH_SIZE)
- 官方建议区分冷／热运行、复用分析与缓冲；使用 Host 计时测异步阶段时需要完成等待。
  本项目保留非插桩正式计时与单独诊断，不能将 Host API 累积秒数称为 kernel 时间。
  [性能提示](https://docs.nvidia.com/cuda/cudss/tips_and_tricks.html#cudss-performance-tips-label)

## 对当前实验的含义

不改动已冻结的 BTF_COLAMD、GPU-only、IR=2、IRTOL=0、原矩阵残差检查及三 ODE。
官网建议的 hybrid CPU/GPU 执行不在本次 GPU-only 验收范围内，不作为隐藏回退。

先实际验证已经准备的五类 kernel 批量启动，检查状态／输入不变、静态资源和端到端耗时。
若仍然偏慢，再用隔离的 API／工作量观察器区分显式 kernel 提交、provider 阶段及
ODE 恢复阶段的等待，并如实计入整个 cohort 的额外 factor／solve system 数。
源码结构和文档均不能替代这一步实测。

条件性后续假设（**未实现、未验证**）：若 whole-cohort 库调用仍是主要成本，
可再评估有界 block-diagonal 打包等执行表示是否减少库开销。
该方案不得增加物理耦合，必须保持原输入／残差和因子世代隔离，并重跑完整合同；
不能现在就承诺它比 non-uniform batch 更快。本页没有授权更换求解器或浮点设置。
