# ARCH 本地应用接口

本目录提供 ARCH 可执行程序的本地配置检查、模型检查、初始场预览和初始 AMR 查询接口。Host 通过受控子进程调用 Core，Studio 消费同一套响应；接口使用 CGS，客户端从选定程序查询版本、参数和能力。

## 接口文档

- [配置检查](docs/CONFIGURATION_API.md)与[配置传输协议](docs/ConfigurationProtocol.md)：必填条件、缺失值、来源、诊断和输入身份。
- [模型检查](docs/CASE_INSPECTION_API.md)：注册模型、Setup/Init 检查及其覆盖范围。
- [初始场预览](docs/PreviewApi.md)：单次 CLI、采样顺序、字段、单位、资源限制和错误响应。
- [初始 AMR](docs/INITIAL_AMR_API.md)：原生初始单元布局、坐标和采样。
- [预览会话](docs/PREVIEW_SESSION_API.md)：持续编辑中的资源复用、取消和响应身份。

公开 C++ 入口位于本目录的 ApplicationContract.h、Configuration.h、CaseInspection.h、Preview.h 和 PreviewSession.h；实现按 configuration、inspection、preview、session、protocol 和 resources 分类。

## 构建与使用

按[构建指南](../../docs/guides/Build.zh-CN.md)构建 ARCH。客户端先调用 `ARCH --preview-capabilities` 和 `ARCH --config-schema` 协商功能，再按对应协议提交请求。参数与模型数量以当前程序响应为准。

配置检查发生在 Setup 和后端构造之前；预览使用 CPU，不因配置选用 CUDA 而初始化设备。初始场或初始 AMR 查询成功，只说明本次查询范围可用。正式运行、重启和物理可靠性由各自的运行检查与科学验收确认。

相关指引与参考路径：

- [Studio 指南](../../docs/guides/Studio.zh-CN.md)：介绍用户工作流。
- [工程测试](../../studio/tests/README.md)：说明 Host 与界面的检查边界。
- [验证索引](../../validation/README.zh-CN.md)：提供数值验证入口。
- [archive](docs/archive/README.md)：保存历史交接材料。

示例按各目录标注的程序与输入身份使用。
