# JENS CUDA 私有候选：有限授权下的验证计划
状态：待验证。用户2026-10-06批准隔离验证；不是公开能力 release。
精确 base ccfcef5bc60f4361a808dc1e83c020094ab50930。
候选 patch validation/gravity/candidates/jens-cuda-20261006.patch，
SHA256 c026244a12f55900062aa32f213466d6d07dddf388eb18695424b0f2726bb123。
候选 ref = 本次交付 Git commit + 上述 path/blob + base；候选不是主分支完整源码。

## 隔离与身份
prepare_jens_cuda_candidate.py 从精确 base 归档 tracked 构建源码/原测试/campaign，
在 studio/.local/integration 下私有 snapshot 应用 inert patch；不新增 Git worktree。
public Core/API/Driver、build-cpu/build-cuda 缓存及 ELF 的前后 SHA 完整保存。
EOS 数据仅引用原路径；依赖来源可复用已安装包/HighFive source，
不复制 public build tree，不用 public ELF 冒称 candidate。
新 private configure/build 必须记录 source inventory、cache、compiler、deps、ELF SHA。
现有 public CPU ELF 32f7b13972ac9381076b06e04c581900065a246796e911bdd161479d86f31b8e；
public CUDA ELF 1cbbd6952f4970f89ef5071b9e3f18d9ec619ec92824f0ad9928455f6e27eb53。

## patch 精确边界
10文件，仅共享 backend 可用性判断、配置/API、Driver device resident 启动顺序，
原 BoxCampaign 入口及原配置 fixture；数学/G/阈值不变。
明确 cuda+Cartesian+self 路径，auto/原生坐标仍拒绝。
候选内可用性不等于公开版本能力；禁止把该 patch 应用于正式源码来提前开放。

## 执行顺序和限制
先 candidate CPU scoped configuration/API/checkpoint 与 frozen9演化+9续算，
再 candidate CUDA；不重复没有变更的 public baseline。
uniform-lifecycle-1：disabled/output-only/active ×1D/2D/3D，t=0.02s，
真实 checkpoint=0.01s，再实际续算到0.02s。
原 native fields/EOS、mass/E<=1e-12、repair=0、physical residual、16epsilon、
strict restart、首次推进前细化、接受宏步、重网格与父态 veto 全保留。
既有内部 t=0 事务检查单列，不能代替应用演化证据。

计划限制：compile28 jobs、heavy CUDA1，单 build timeout3600s；
MemAvailable>=4096MiB、swap growth<=256MiB；
OMP1，原单运行 timeout1200s、总 campaign14400s，
raw10GiB、VRAM10GiB/GPU0；实际 wrapper 实施后再标记 enforced，
准备阶段不冒称已经施加。触发资源/错误/超时保存日志并停止，不 partial PASS。
输入需逐个 SHA；运行前再次记录资源、准确命令与实际终止条件。
若自动审批拒绝，报告原理由，不换 wrapper/入口绕过。

## 出口
实际完整短包成功 → 处理后身份/指标提交 Core review → Core 决定 public 开放范围。
失败分项保留；不启动新的 RZ 或未确认 benchmark/长包。
本计划与三份 RZ 决策、连续面力参考、benchmark 输入提议分别验收。
