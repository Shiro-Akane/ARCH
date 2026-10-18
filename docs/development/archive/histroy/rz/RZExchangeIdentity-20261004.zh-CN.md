# RZ seam chart 与 GhostExchange 缓存身份

## 实际改动
GetPlans/ExecuteExchange 接收显式 CoordinateSeamGeometry，默认 ExistingChart
保持原调用方行为。RzAxisymmetric 经现有完整 Host exchange 执行器调用同一 seam plan。
缓存 exact key 加入 chart 与各叶 native geometry 字符串、维数、bounds、
dx、root counts、ghost depth；double 保留 binary64 位身份。
逻辑 UID/epoch/neighbors/species 既有 key 保留，field/slot/指针内容不加入 key。
因为 seam 已缓存 donor/stencil weights，不能仅凭拓扑未变假定物理位置也未变。

## 证据
基线 69fc5e6aed09f7e6a2af7a721c44c6056092ed40 加 Summary 中输入。
仅构建 arch_amr_operation_plans；CTest amr_operation_plans 1/1 PASS；
git diff --check PASS，无独立 configure、ARCH build 或 simulation。

真实均匀与混合 L0/L1 网格验证：
- default polar chart 无 RZ seam；改 chart 同一拓扑重建。
- 同 chart/native geometry 保持缓存命中。
- 整域半径平移到非零内边界使 axis plan 失效；恢复轴重建。
- 完整 Host ExecuteExchange 后 scalar/z 偶、r/phi 奇保持。
- 切回 ExistingChart 不残留 RZ seam。

既有 slot/storage rebind、非法 layout、species/epoch change、
polar/spherical seam 和 AMR transfer regression 同一测试通过。
日志/原始输入 fingerprint 留 studio/.local/integration/rz-exchange-identity-20261004；
处理后身份见同名 Summary.json。

## 未覆盖
这是 chart 身份和轴 ghost 执行链的工程验证。粗细层传输仍使用当前生产度量，
因此不称为 RZ restriction/prolongation/reflux 保守验收。
Driver/CUDA lowering、geometry semantic revision、完整 RZ metrics/transport、
重力边界和 IO/checkpoint 尚需成套迁移；runtime capability 未开放。
未执行 CUDA、演化或新科学预算检查。无 push/tag/main merge。
