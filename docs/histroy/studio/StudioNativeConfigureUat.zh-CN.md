# Linux 原生 Configure 验收

2026-10-03，开始源码1fca3381f039a83d2c3ae71ec79e6e4d950ef25c，工作树clean。
依据StudioConfigurationHandoff第4.2、5节，补齐原生Configure操作证据；
此前Host真实执行和自动回归继续复用，不冒充原生UI验收。

用既有Linux arch-studio entry、production assets打开当前项目及保存的Sod测试副本。
未启动Vite或Windows launcher。startup按现有流程执行init-only Preview，
待Preview succeeded、无活动Run后通过原生Configure按钮一次启动。

Operation c160d385-b4bc-4ea6-ad67-0a1671486fbd，
Host-owned profile studio-cpu-release。窗口捕获Configuring/running及Cancel Configure，
随后succeeded；展开terminal drawer观察真实CMake Release配置/生成日志。
GET configure/status记录succeeded/exitCode0和真实CMake File API输入证据。
完整输入列表和events留ignored目录，提交精简数量、hash和状态。
Hide drawer操作发生时任务可能已完成，不能据此宣称native active-hide覆盖。

Cache确认：
- source root /home/arch/projects/ARCH-compute-optim
- build directory /home/arch/projects/ARCH-compute-optim/build-studio-cpu
- Release / Ninja / ARCH_ENABLE_CUDA=OFF
- ARCH_EMIT_LINK_DEPENDENCIES=ON保持既有profile设置

CMakeCache、CMakeLists、CMakePresets、保存输入及ARCH binary的SHA/size/mtime
前后均完全相同。binary仍e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7。
没有clean、修改编译选项、ARCH target Build、CUDA、Run/Restart或Save；
Configure自身执行正常CMake探测/生成，不将它称为“没有任何编译器探测”。
新鲜度仍freshness-unknown，dependenciesComplete=false；配置证据不替代链接/编译身份。
无相关输入/binary变化，不为凑验证重复Build。

**原生CPU Configure：PASS。** 3C其他剩余验收及科学待审不由此关闭。
当前窗口/Host仍运行，未主动关闭用户窗口。产品代码未修改，
只执行report JSON格式和git diff --check，不重跑234项未变检查。
原始本地证据studio/.local/integration/native-configure-uat；
无raw data上传、push/tag/main merge。
