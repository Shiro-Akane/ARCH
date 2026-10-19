# Host CPU LTO 取证接入与真实构建

源码基线5ba4a9579517ef5aa664fc910ad74b5009caff45，构建时工作树clean。
本轮走已有ConfigureRunner/BuildRunner/localCpuProfile，
GNU CPU Release、parallel4，保持IPO和科学数值flags。
启用已验证取证选项；仅retention profile给编译器Host-owned私有TMPDIR，
目录为build-studio-cpu/.studio-link-inputs/<Host build UUID>、mode0700。
拒绝根目录符号链接后才启动编译器，浏览器不能传env/command/tmp path。
保留对象不自动删除，不提交生成binary或原始LTO文件。

完整Studio/Host258/258、typecheck/lint/diff-check通过后本地提交，
随后真实Configure成功、Build92b66ad1成功。前后ARCH SHA256均
ee3de6cf2b8cd54bc54e70a5c15ffdaa60a9fb39281ae243800f21d1286c2d5a，
完整bytes未变，不只是.text相同。科学Core未修改、未运行simulation。
735 compiler inputs、toolchain与compiler inputs across-build stability证据保留。
linker输入172项全部fingerprint，原97个缺失对象全部在私有目录真实存在，
unavailable从97降为0。准确大小/时间/身份见Summary。

原verification Node退出后，新的BuildRunner从disk加载Manifest并重新fingerprint，
missing0、changedInputs空，保留LTO文件仍存在；不是只在原进程尚存时取得的证据。
Studio production build通过，日志留ignored .local integration目录。

仍保留dependenciesComplete=false、freshness-unknown，理由为全依赖coverage未知。
完整配置生成器/外部CMake输入和必要工具链消费者尚须系统性核对，
不能因为解决LTO或binary逐位一致就宣称干净构建、完整release或科学验收完成。
没有为该诊断重跑binary未变的Core/全模型Preview套件；既有证据保持原build身份，
新的工作流应使用本次BuildID，不将旧field/AMR冒称新请求Current。
未push/tag/main merge、未运行CUDA、未上传任何原始输出。
