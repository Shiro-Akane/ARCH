# 选定链接器身份接入 Build Manifest

2026-10-03，Linux/WSL。前置提交 f42fa05493350daafefc02fdf92d015e31c7a3e8。

## 实现与边界
readBuildToolchainEvidence 在 GNU CXX 的 ARCH target 上读取 codemodel 链接选择，并通过 GNU driver 查询对应 programName。选定程序作为 selected-ld.mold 等独立组件进入既有 compilerDrivers / preBuildCompilerDrivers、sameToolchain 和 reload freshness 链。
探测与 Build 统一使用 Host 固定 PATH /usr/local/cuda-12.8/bin:/usr/local/bin:/usr/bin:/bin；不执行浏览器命令或 CMake command fragments。
这记录配置选择及可解析程序的身份，不声称观察到实际链接器进程执行。依赖完整性保持 false；动态隐式库等覆盖仍待完成。

## 本机证据
GNU driver 返回 ld.mold，Host PATH 解析 /usr/bin/ld.mold，realpath=/usr/bin/mold。
SHA-256=9e23dde239d96e75691c107d0f387039aae7698ca73defec6080ada0eb920723。
size=19054816 bytes。

## 行为验证
- 构建前后身份保存并比较。
- 重载后选定链接器哈希不匹配：needs-build，changedInputs 指向实际路径。
- 旧 Manifest 缺少 selected-* 组件：freshness-unknown，不追认、不伪造 inputs changed。
- GNU subprocess/specs 旧回归仍通过。
- npm test 260/260 PASS（包含 Host，不重复计数）。
- lint PASS；npm run build 含 tsc --noEmit 与 Vite production build PASS；diff check PASS。
- 两次测试暴露的夹具问题已修正：role 不是实际路径；手工写入 persisted Manifest 后须创建新 Runner 才验证 reload。
- Vite 现有大 chunk warning 保留，未做无关打包重构。

## 状态与下一步
旧真实 Build Manifest 未改写，未执行 ARCH configure/build 或 simulation。
下一次正常实际 Build 将生成包含选定链接器的新证据，再验证构建前后稳定性及重新启动 Host 的重载结果。项目科学验收、CUDA 和长轨迹仍未完成。
完整本地日志留在 studio/.local/integration/linker-manifest-20261003/，不提交原始输出。
