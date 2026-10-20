# ARCH 实际链接选择取证进度

2026-10-03。基于集成提交 1f80abd863fbe97d96257bf8f0ddeecd6f9944aa，Linux/WSL。

## 已核对
- CMakeCache 的 CMAKE_LINKER=/usr/bin/ld，不能据此认定 ARCH 使用 GNU 默认链接器。
- 当前 ARCH codemodel flags 和 Ninja target link command 同时包含 -fuse-ld=mold。
- GNU driver 的 -print-prog-name=ld.mold 返回 bare name ld.mold；本机 /usr/bin/ld.mold 解析到 /usr/bin/mold。
- GNU -### 只输出 driver/collect2 调用计划，不执行编译或链接，也不证明 collect2 实际执行的子进程。

## 本次实现
Host 增加 readBuildLinkerSelection：从选定 build tree 的 File API codemodel 读取唯一 ARCH executable/CXX target。记录 codemodel 与 target reply SHA-256；不执行 command fragment。
仅支持已明确的默认、mold、lld、bfd、gold 选择。多个选择、路径形式选择、-B、sysroot、specs、response file 及引用/转义形式均拒绝；多配置树和目标路径越界拒绝。
返回 observedExecution=false；这是配置选择证据，未接入 Build Manifest，不升级旧 Manifest。

真实 codemodel SHA-256：934f6a663ae8b1886b53d8b9ca84c16f77f9bd31570af828c44f7d3936cfe4f6。
真实 target reply SHA-256：2a21a2b0f661f3a9142d24fb83e3e629c51dbd51b113920f8912a870e819483c。

## 验证
- CMake evidence 针对性测试 5/5 PASS，含选择、无覆盖默认、冲突选择、解析覆盖及目标越界。
- npm run typecheck / npm run lint / git diff --check PASS。
- 真实 build tree 只读调用返回 -fuse-ld=mold / ld.mold。
- 未重新 configure/build ARCH，未运行 simulation，未生成科学输出。

## 未完成与下一步
采集选定链接程序的路径、解析路径和内容指纹，验证实际 driver 搜索与 Host 环境一致，并接入构建前后身份和 reload freshness。无法建立真实选择绑定时保持 unknown。
这一步不能取代实际执行观察；动态/隐式库、其他每调用覆盖和全依赖覆盖仍不完整。
现有依赖完整性标志保持 false，旧 Manifest 不重写、不追认新证据。
