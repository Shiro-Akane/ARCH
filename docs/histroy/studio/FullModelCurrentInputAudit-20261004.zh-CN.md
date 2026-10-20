# 全模型旧输入的当前静态契约复验 — 2026-10-04

本轮 source 4e98668f21d6ed144489582ebd37be3b2776b3a6，managed source 64b0ce2f8d97553f59024978618f1e4848a974e1，实际 CPU binary SHA-256 d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75。使用独立干净 managed project 的现有成功 Build，不构建、不运行 Setup/Init/AMR/simulation，不重复已通过的334项Studio回归。

## 方法与准确覆盖

逐份读取本地 full-model-production-20261003 输入的原始字节，执行 ARCH --inspect-config <registered case ID> --config-stdin --request-id matrix-<profile>。只读 --list-cases 确认当前14个registered case集合与16个输入profile的case集合完全相等。Gaussian的3份输入共享实际case ID Gaussian；不根据统计数硬编码生产模型列表。

每份保留输入SHA、exit/status/completeness/coverage/diagnostics/耗时。16份中的15份complete/exit0，CellularDet incomplete/exit3。不是16/16全部通过。实际execution全部 setup=not_executed、eos=not_loaded、cuda=not_initialized、simulationReadiness=not_checked；静态complete不能提升为运行就绪或所有物理域支持。

| Input | Case | Exit | Declared completeness | Diagnostic |
|---|---|---|---|---|
| BurnGradient.par | BurnGradient | 0 | complete | — |
| BurnOneZone.par | BurnOneZone | 0 | complete | — |
| CellularDet.par | CellularDet | 3 | incomplete | tmax: MISSING_PARAMETER |
| CooperativeHotspots.par | CooperativeHotspots | 0 | complete | — |
| DiffusionMode.par | DiffusionMode | 0 | complete | — |
| ExternalGravity.par | ExternalGravity | 0 | complete | — |
| Gaussian.par | Gaussian | 0 | complete | — |
| Gaussian3D-spherical.par | Gaussian | 0 | complete | — |
| Gaussian3D.par | Gaussian | 0 | complete | — |
| GravityBox.par | GravityBox | 0 | complete | — |
| JeansWave.par | JeansWave | 0 | complete | — |
| RT.par | RT | 0 | complete | — |
| SNIaCoupled.par | SNIaCoupled | 0 | complete | — |
| Sedov.par | Sedov | 0 | complete | — |
| SmoothAdvection.par | SmoothAdvection | 0 | complete | — |
| Sod.par | Sod | 0 | complete | — |

## 旧输入 finding 与替代输入

旧CellularDet缺tmax，当前共同解析明确MISSING_PARAMETER；原始文件保持，未借新默认回填或放宽validator。旧FullModelProductionHostProgress的全成功结论只属于其准确旧binary/input，不能移植为当前clean64b0ce2f binary证据。

另对现有已批准t=0输入只读检查：SHA-256 6ca2b63719942d1a900cab8458a4417b0dc88c3a41750e37df5fff55647391e3，exit0、declared complete。该输入tmax=0 / use_burn=false，此前CellularPlotMarginNative已用其完成二维原生复验。它不是旧burn-on输入的等价运行迁移，也不能使旧失败行变PASS。若要将旧目录作为新正式运行包，tmax和运行物理终点必须沿冻结输入来源指定；不得猜一个终点后开始演化。

## 复现与数据

本机原始响应保存在 studio/.local/integration/full-model-current-inspection-20261004。使用干净managed CPU binary，从WSL可逐份以stdin重放；原输入不修改。没有上传原始H5/plt/checkpoint/完整场数组，提交只含处理后的静态摘要和本文。

本次耗时是静态inspection进程墙钟，不能用作模拟、冷EOS加载、Preview或CPU性能benchmark。registered 不等于任意维数/几何完整场支持，配置complete不等于科学验收通过。其余模型的原生Init/AMR报告继续使用各自输入/构建身份，不能由本次检查升级为当前全回归。

managed worktree保持clean。无源码修改、push/tag/main merge或Windows工作；CPU科学待审、CUDA/O9、原生wheel/pan及连续组分场等未闭合项保持原状态。
