# 成对终点 runner 的冻结输入与旧证据保护
基线6b3aa375e09f0bd473ac15d86061cf13f19a8363；仅既有执行工具/合成测试变化。
本轮不启动ARCH、不执行CPU/CUDA科学轨迹，不改CFL、floor、EOS、终点或误差预算。

## 已证实缺口与修复
旧 main 以 exist_ok=True 创建output，pair/label逐项解析后才启动；复跑可能先覆盖summary，
随后才因单次目录碰撞失败。label没有路径/重名限制，可能逃逸输出根或占用summary.json。
旧 paired_trials 每次one_run都从原source重新读取，原文件被编辑后会改变后续backend输入。

现在先验证全部specification与binary，再以exist_ok=False原子取得新的output目录。
label仅为有界单层名称、不得重复，不允许summary.json保留名；路径带空格仍是独立argv，
没有shell执行。已有根目录（包括旧summary）、已有pair目录或binary缺失拒绝，
不清理、覆盖、自动改名或恢复旧run。

每个pair开跑前按原bytes保存 frozen-source.par，记录原source path和SHA；
仅该快照供预热/全部CPU/CUDA测量。快照chmod0444，启动每个backend前和完成后核验SHA。
原source之后被编辑不影响当前pair；快照被修改明确失败并停止后续backend，
保存attempt/status/error及已有证据，不再跑一遍掩盖失败。
readonly权限不是防恶意修改的安全边界，SHA检查也不提供全文件系统的瞬时事务保证。
记录raw-source SHA与每次generated input SHA分开，允许明确backend/out_dir/stopping-mode覆盖。
保持原step模式、endpoint核验、repair/residual/parity预算、预热交替与负收益保留语义。

## 真实检查范围
24/24 targeted tests PASS，无skip，包含此前19项与5项新增：
- 原source在首次run后修改，8次调用仍收到同一快照字节；
- 故意解锁并修改快照，只执行首个stub，下一backend没有启动且状态failed；
- 已有summary字节不变；路径逃逸/absolute/空格label/重复/保留名拒绝且无output；
- 已有pair frozen-source字节不变；
- 缺binary在创建output/执行pair前失败。
原终点早停、repair、Poisson残差、parity超限、绑定失败及kernel child绑定检查仍通过。
--help与git diff --check PASS。详细源码SHA见Summary；完整日志留ignored .local。

这是每pair的输入/目录保护，不是完整外部EOS数据、library或build冻结。
实际二进制运行身份、完整manifest、CPU-only对照、实际team/resources采样仍须完成；
qualified_benchmark=false。不能把合成stub/H5的24项PASS当科学或性能验收。
Core批准的Jeans/RZ定义、预算与CPU gate仍等待；本轮不执行CUDA/O9、不push/tag。
原始snapshot/H5/plt/checkpoint/ELF或完整场数组不提交，仅处理后摘要和脚本。
