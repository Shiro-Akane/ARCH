# Jeans 指数边界 CPU 补证

基线 01cbe4376b317e217333b1f75ba119a25eb43627。按 JeansRZPlatformHandoff §3
对极低正密度、指数抵消和无法表示结果补充独立数值检查。
本步没有生产消费者接线或能力开放。

## 参考与覆盖

扩展 validation/gravity/jeans_numeric_reference.py，保留原默认 JSON 输出，
新增 --cpp 生成确定性 C++ 参考。独立 Decimal 80/120 位计算使用已发布
CGS G 和高精度 pi，不调用生产数学/EOS/GridMetrics。两精度舍入一致，
生成结果与提交的 tests/math/physics/JeansNumericCases.h 完全一致。

密度与声速平方指数覆盖 -1074/-1022/-600/-1/0/600/1023，
间距指数覆盖 -1074/-1022/-600/0/600/1023；合并原样本后301项。
197项正有限结果（含2项次正规），31项最终舍入为0，73项为infinity。
上/下溢同时检查 unrepresentable 状态和实际0/infinity结果，不能混淆两类。
正有限结果继续沿用原16*double epsilon相对误差工程界限；
误差在long double中计算，避免误差检查自身下溢。
没有新增物理floor、放宽阈值或替代一般EOS科学预算。

## 结果与执行

cmake --build build-cpu --target arch_jeans_diagnostics --parallel 4
ctest --test-dir build-cpu -R '^jeans_diagnostics$' --output-on-failure -V

最终无警告构建，CTest 1/1 PASS；301项最大相对误差
1.5432483614913696e-16。原非法输入、物理间距和24项IdealGas检查仍通过。
首次构建发现新增输出换行写成多字符常量；修正后重建并保存最终日志。
生成夹具一致性及 git diff --check PASS。

完整日志位于 studio/.local/integration/jeans-exponent-grid-20261004；
精简身份和结果见同名Summary。未构建生产ARCH、未运行simulation、
未生成科学输出、未执行CUDA、未push/tag/main merge。

## 范围与下一步

状态 engineering-pass；O7.1整体仍不成立。一般EOS声速语义、候选父态
规则和独立科学预算仍需Core确认。RZ近场方法/开角/阶数/误差账本也保留
计划要求的科学确认门槛。不能用此数值补证宣布生产JENS或RZ可用。
