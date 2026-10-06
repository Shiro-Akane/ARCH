# Plotfile 未知来源身份原因贯通

## 阶段与改动

联合计划 3C 后独立 Plotfile 小切片；首版继续限定 Sod 1D 与 Cartesian 2D AMR。
基线 4aea1139351157054a10e65a16b77e2701ccda79；owner contract
23ff77c4f08419de2b3c5eadee214da2af25784e。未 merge、未 push。

真实 writer 原来记录三个 unknown 值但没有逐键原因。本次增加可选属性：
- SourceIdentity/effective_config_sha256_reason：authoritative effective-config identity not supplied to writer
- SourceIdentity/build_id_reason：authoritative Build Manifest identity not supplied to writer
- SourceIdentity/source_git_head_reason：authoritative source Git identity not supplied to writer

对应值继续为 unknown。Reader 将原因透传为 unknownIdentityReasons，
client 校验完整三键、非空、无 NUL、每项不超过 256 字符；不完整或非法声明拒绝。
旧文件完全没有三个 reason 属性时保持兼容，不从当前项目推断身份。
Viewer 分别展示 Effective config / Build Manifest / Source Git 的原因。
旧文件显示 reason not recorded in this file。

本次不改原始场值、FP64、单位、分量基底、原生 bounds/measure、
发布方法、科学初始化/演化或 checkpoint 语义。文件布局仍采用分散属性。

## 实际验证

- 只增量编译 arch_plotfile_publication：HDF writer 与 IO 测试两个对象及 test link。
- CTest plotfile_publication 1/1 PASS；实际 C++ 写出及已有故障检查通过。
- C++ writer 的 native-1.h5 / native-2.h5 经 production Reader → client，
  三个原因精确一致，buildId=null，renderEligible=false，读取前后文件 SHA 不变。
  这是 IO fixture，不是 Sod/Cellular 科学输出或原生桌面 UAT。
- 来源身份定向反例 9/9 PASS；完整 Studio/Host 327/327 PASS，0 skip。
- lint / typecheck / production build / git diff --check PASS。
  既有 bundle chunk-size 警告保持，没有打包重构。
- 首个 Node 检查命令因 WSL PATH 中带空格而 shell 解析失败，未启动测试。
  改用明确 Linux PATH 后正常执行；不将命令失败算作测试通过。

处理后摘要见 PlotfileIdentityReasons-20261004.Summary.json。
原始 fixture 位于 build-cpu/plotfile-publication-data，日志位于
studio/.local/integration/plotfile-identity-reasons-20261004；全部留本机。
主 build-cpu/bin/ARCH 没有重新编译，仍是摘要记录的旧 ELF；
不能声称该 executable 已包含本次 writer 属性增量。

## 清单与未完成项

- [x] 文件中的未知身份有可读原因；旧文件可读，非法原因不绕过校验。
- [x] 真实 C++ IO → Reader/client 跨实现读回。
- [x] 受影响前端/Host 回归、production build；处理后证据提交。
- [ ] authoritative effective config、Build Manifest、source Git 绑定：仍缺，
      本次仅说明缺口，绝不把 unknown 变成 known。
- [ ] 完整单位/基底/低维积分和独立科学 oracle review。
- [ ] 二维演化 AMR、真实大文件索引/cache、首次扫描成本。
- [ ] 真实 ENOSPC、fsync/断电持久性，均不由既有注入故障替代。

固定显示像素数继续只约束返回量；首次总览仍可能扫描大量叶块。
曲线坐标、3D、XDMF、CUDA/O9 和总项目验收没有由本次证据关闭。
