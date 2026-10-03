# GCC LTO 链接证据缺口

2026-10-03。当前production Manifest build b122233f记录735 compiler inputs、
75持久link inputs、97缺失link inputs。编译/工具链前后稳定已有证据，
但缺失LTO临时对象不能被文件名白名单忽略。dependenciesComplete仍false；
tracked inputs未变不等于完整freshness已证明。

独立非科学C++双文件probe使用现有g++/mold、O3/LTO及save-temps=obj，
编译链接成功，未运行probe可执行文件、未构建ARCH或运行simulation。
GCC保存probe.ltrans0.o，但真实depfile记录probe.ltrans0.ltrans.o。
严格fingerprintLinkDependencies得到18项持久输入和1项missing，complete=false。
不能复制/重命名近似文件后声称它是链接时读入的对象，也不能事后猜相同哈希。
准确命令和结果见Summary；probe二进制/日志只在ignored本地目录。

因此没有把save-temps加入ARCH flags或重新configure现有build tree。
未来完整capture需在真实linker消费期间记录临时输入内容/身份和生命周期，
并验证最终depfile关联；还须覆盖构建生成器/外部CMake输入，不能仅解决这97项
就设置dependenciesComplete=true。保留现有科学flags和IPO优化，不以关闭LTO凑通过。

本步Build freshness文案显示实际missing数量；缺失证据仍unknown。
相关Build/安全/Link/Ninja测试、typecheck/lint/diff-check通过。
没有改写旧Manifest、推送、tag或main merge。新诊断不是clean-from-scratch构建认证。
