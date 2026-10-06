
# benchmark / 长轨迹输入候选：未冻结、未执行
基线 ccfcef5bc60f4361a808dc1e83c020094ab50930，14700K + RTX 4070 Ti。
本表提出明确物理终点，等待 Core 确认；墙钟仅为失败保护，不能代替物理终点。
原模型的 bounded smoke 通过不证明这些移除 max_steps=5 限制的新轨迹已经签收。

| 候选 | 终点 s | 实际 checkpoint 分割 s | 根块 | nominal L1 shape | 墙钟保护提议 | 磁盘提议 |
| --- | --- | --- | --- | --- | --- | --- |
| SNIaCoupled 2D benchmark | 1e-7 | 5e-8 | 2,2,0 | 64×64 | 1200s | 10GiB |
| SNIaCoupled 3D benchmark | 1e-7 | 5e-8 | 2,1,1 | 64×32×32 | 1200s | 10GiB |
| SNIaCoupled 2D long | 1e-6 | 5e-7 | 2,2,0 | 64×64 | 7200s | 30GiB |
| SNIaCoupled 3D long | 1e-6 | 5e-7 | 2,1,1 | 64×32×32 | 7200s | 30GiB |

长包 1e-6 是供 review 的 10 倍时间跨度提议，不是已批准的科学终点。
均为 Cartesian [0,1]^dim cm，每块16，L0..L1、pool128；实际 mixed AMR 不冒称全域 L1。
2D periodic/self periodic；3D outflow/self isolated。完整源输入、全部派生输入、
EOS 表及 aprox13 文件 SHA-256 见 input-proposals.json，不依赖本表隐藏默认值。

EOS=Helmholtz，单独原表路径/哈希；C12/O16=0.5/0.5，aprox13，burn，
BD/DenseLU、rtol1e-4/atol1e-8；thermal diffusion/RKL2；
RK2/CFL0.3、HLLC/MUSCL-MC，regrid=2，原 DENS 阈值不变。
初末 Plotfile，checkpoint 按分割时刻输出，续算实际到同一终点；
禁止仅按文件名判断 checkpoint 时间。原始数据和全日志留本机。

资源提议：RAM12GiB、VRAM10GiB；磁盘和墙钟如上；先检查系统可用内存、显存与磁盘，
达到上限明确失败，不以 cap 停止冒称科学完成。以上预算均待确认/尚未实施。
CPU thread screen 候选8/16/28，CPU affinity 由实际拓扑记录，不猜 P/E core；
确认后固定线程和 affinity。统一 CUDA Release ELF 的 CPU/CUDA 配对，
另保留 CPU-only baseline；各 warm1 次，至少3组交替串行计时，禁止并发科学计时。

独立检查：原 verify_coupled 的 native/AMR/ENUC/repair/residual/实际终点，
compare_backends 原场2e-7、ENUC1e-5、species1e-9。后端一致不是独立物理参考。
新的完整耦合轨迹/开放边界能量参考和验收预算仍需 Core 指定。
JENS frozen uniform-lifecycle-1 另行候选验证，不用这个 Helmholtz 包扩展 JENS 科学声明。
RZ 未签收，不纳入候选；旧 polar 不作为 RZ；缺16必填键的 P13 不填默认值。

## 请逐项确认
终点/分割时刻、现有短科学 gate 是否覆盖派生完整终点、独立演化参考/误差预算、
资源/线程/timing 规则。确认前不运行这四份提议输入。
