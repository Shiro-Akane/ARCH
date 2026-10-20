# Jeans 数值基础：Native Tabular 守恒态解析对照

基线：7473158e6e8be1faff92adbd9016616b1fbf6e3a。Linux/WSL CPU。

## 改动与依据

仅扩充现有 tests/host/eos/NativeTabularRegression.cpp。
复用 NativeTabularFixture 的非均匀 rho/T/Ye 节点和 log-linear caloric law：
P=rho*R*T*exp(a*Ye)，e=Cv*T*exp(a*Ye)。
40 个域内 rho/T/组分状态 × 静止/运动 × 3 个间距 = 240 项。

以手工守恒态构造调用真实 Tabular3DEOS get_pressure/get_sound_speed，
随后调用隔离 JeansDiagnostics::evaluate。独立长双精度参考由 caloric law、
literal pi 与 CGS G 构成，不取生产 EOS derivative 或生产 Jeans/constant 作为期望值。
保留 fixture 的 2e-12 算术容差；不是新增／调整一般 EOS 或演化科学预算。

## 验证

cmake --build build-cpu --target arch_native_tabular -j 2
ctest --test-dir build-cpu -R '^native_tabular_eos$' --output-on-failure -V

Scoped target 编译成功，CTest 1/1 PASS。
JEANS_NATIVE_TABULAR_CASES=240
MAX_RELATIVE_ERROR=1.0004323370200706e-14

既有 native fixture 的值／导数／反演／拒绝路径在同一测试中保留并通过。
本轮未传真实表文件的可选第二参数；不能声称真实 EOSDriver 表通过。
H5 fixture 在 build-cpu/native-tabular-test-data 保持本机；日志在
studio/.local/integration/jeans-native-tabular-20261003/LastTest.log。
不提交原始 H5、全量数组或日志。精简身份见同名 Summary.json。

## 出口与限制

这是额外的静态接口链路证据，不能替代 Core 对一般 EOS 声速含义、
候选父态、条件需求、参考与预算的确认。
未修改 EOS/Jeans 实现、能力目录、AMR 或 scientific Core，JENS 仍 unavailable。
未重编生产 ARCH、未运行 simulation/Preview/CUDA、未 push/tag/main merge。
Helmholtz、free-energy Tabular、真实表、演化与 restart 保持未验证。
