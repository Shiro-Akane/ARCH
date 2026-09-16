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

另一个具体的只读发现：`SparseWrap.h::SparseKLUSolver::factorize` 在已有 numeric 对象时
先调用 `klu_refactor`，失败后才重新 factor；现有 CUDA provider 及 v4 候选对新矩阵值
仍调用 `CUDSS_PHASE_FACTORIZATION`。这不是证明两端工作量或耗时差异的全部原因，
但比笼统归咎于显卡／稀疏矩阵更值得测试。
官方将 `REFACTORIZATION` 列为 factor 后可选阶段，并说明它在 BTF_COLAMD／COLAMD
下才与普通 factor 有区别。[阶段说明](https://docs.nvidia.com/cuda/cudss/types.html#c.cudssPhase_t.CUDSS_PHASE_REFACTORIZATION)
[调用顺序](https://docs.nvidia.com/cuda/cudss/functions.html#c.cudssExecute)

因此 kernel 批量提交完成后，优先评估一个独立的 refactor 阶段候选，而不是立刻做
更大范围重排。必须先证实本版本的 non-uniform batch 支持、数值稳定性和状态失效恢复，
保留原始残差、全部错误检查与 factor 工作量计数。**当前没有实现或测试该改动**，
不能将文档支持写成真实核反应矩阵已经通过，也不能把任意库错误隐藏为“可重试数值失败”。

更远的条件性假设（**未实现、未验证**）：若 whole-cohort 库调用仍是主要成本，
可再评估有界 block-diagonal 打包等执行表示是否减少库开销。
该方案不得增加物理耦合，必须保持原输入／残差和因子世代隔离，并重跑完整合同；
不能现在就承诺它比 non-uniform batch 更快。本页没有授权更换求解器或浮点设置。
