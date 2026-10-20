# 全模型初态实施记录：按维度采样基础

2026-10-03，基线fa9d63dbf2b23d06dcb1c20fcb7e056a870a3878。
按FullModelInitialAndPlotContractAudit.zh-CN.md的第一原子步骤实施；整体阶段仍in progress。

## 实现

原所有者src/api/preview/Sampling.h新增ResolveSampling(request, parsedDimension)：
按1/2/3D确定活动轴，所有轴及总点数先校验，再返回nx/ny/nz/count。
dimension不再必须由case名决定，Sod/Cellular旧caller暂保留兼容入口，
当前GeneratePreview/CLI/Session仍沿旧调用与support gate，不宣称多模型/3D已可调用。
PreviewRequest仅增加内部samples_x3；没有发布新CLI flag或model capability。

1D/2D预算与默认512、128x128、每轴256/总65536保持；
内部3D默认32^3，每轴最多64，总点数32768。工程工作预算不是物理采样精度声明，
实际序列化8MiB及CPU/内存/逐模型测试完成前不发布3D capability。
SamplingPlan.two_dimensional只对2D为true，不把3D伪装为2D。

## 验证

真实既有build-cpu，CPU Release、CUDA OFF、CMAKE_HOME_DIRECTORY绑定当前项目；
仅arch_preview_sampling_limits单目标/单编译任务，无独立configure。
编译/链接exit0，new target mtime晚于三个修改输入，preview_sampling_limits CTest 1/1 PASS；
git diff --check PASS。

新增覆盖case-independent1/2D，3D默认、7x5x3、64x32x16总预算边界、
每轴1/65/int_max拒绝、64^3总超限、缺轴、跨维轴选项、混用--samples、非法dimension。
保留既有Cellular非方形、二维上限、Grid inactive coordinate、序列化超限身份保留测试。

首次异步构建没有完成日志且binary未更新；第一次旧target CTest通过不计新增覆盖。
改用同步捕获退出码及target时间身份；后续新增测试auto*数组推导编译失败，已改显式optional<int>*，
没有改预算/测试条件。最终实际重编及CTest通过。所有旧/失败/最终日志保留本机，
studio/.local/integration/dimensional-sampling-20261003；精简状态见result.json。

## 未完成与下一动作

先补三轴grid边界/根容量和native坐标语义，再将parsedDimension接入GeneratePreview，
逐模型实际SetupChecked/Init/shared EOS并发布经过验证的能力；继续Host/UI三维/状态表示及AMR。
plt独立接口仍待实现；不将采样helper当作全模型Preview或科学验收。

当前ARCH binary SHA仍e506619f...03813bc7；没有ARCH重新编译、
新Preview/AMR/simulation/CUDA或原始输出。源码头发生改变后旧binary只代表已编译版本，
后续实际Preview验证前必须更新受影响CPU binary/Build Manifest，不冒称source current。
Host/Studio未改，不重复未变化235项回归；阶段最终回归仍按联合计划要求执行。
科学定义/独立参考/阈值、历史G迁移及architecture待审状态均未改变。
