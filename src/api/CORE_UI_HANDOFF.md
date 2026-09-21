# Core 参数编辑接口交接

## 分支与同步

- 交付分支：`codex/studio-core-ui-contracts`。准确新增提交号随推送后的交接消息提供。
- 冻结 main 基线：`01cc4f723e674d47fe23850e7c0fef221e92e98c`。
- 该基线上已有三项独立 Core 提交：基础 CPU Preview `40b7704d4f583aa5b556f63bdbfe16815a6a7e82`、Core A `47517d1ce0ab33b761fcf3dd1a4241d68b7041fd`、Core B `91a46f8f5498fa207c5210e3a12f366fa270df80`。
- 本轮核对的 Studio 分支：`studio/phase2e-b-cellular-2d`，提交 `fb22178fe578b17120597633f427e38d2a2be582`。其中相关 Core API、RuntimeParams、解析器和模型源文件与上述 Core B 一致。

已有 A/B 的 Studio 分支只需 cherry-pick 本次新增提交，再重新编译。无需重复摘取前三项，也无需为了接口同步合并整个 Core 分支。此分支没有引入 Studio 分支历史；后续合入 main 时，保持 Core 与前端各自的提交范围可核对。

本次增量已针对上述 `fb22178f` 的文件快照完成补丁应用检查，无冲突。构建与接口测试在本 main 派生分支完成；Studio 接入后仍需自行完成 Host / UI 验收。

## 已完成内容

1. `ARCH --config-schema`：提供当前 RuntimeParams 的全部 90 个标准配置键，包含兼容别名；给出类型、解析缺省值、分组、部分约束、选项、单位与路径用途。Network 包括 ODE 的 RTOL/ATOL 等，Gravity、Diffusion 有独立分组。
2. `ARCH --inspect-config CASE --config-stdin`：接收尚未保存的完整参数文本，返回类型转换后的输入、显式/默认/别名来源、适用状态、坐标说明和字段错误。它在 Setup 和策略最终解析前工作，可用于构建编辑器；不应把结果命名为模型最终实际采用值。
3. 坐标与单位：提供三种 geometry 在 1/2/3D 下的轴名称和稳定 x1/x2/x3 参数键；Preview 返回标准场和坐标单位。Helmholtz/tabular 按 CGS，IdealGas 按模型自定单位 `code_*` 标记；标签不转换数值。未知 custom 参数保留单位未知。
4. 输入检查：标准整数不再截断小数或后缀；标准浮点数、坐标表达式要求完整、有限的合法输入。Helmholtz diffusion 的禁止组合返回字段错误。标准缺省值与 RuntimeParams 共用定义；custom 参数的既有读取语义保持不变。

接口说明：[CONFIGURATION_API.md](CONFIGURATION_API.md)。完整真实输入和响应：[examples/configuration](examples/configuration/README.md)。原有 Preview / Sod x_pos 绑定 / CellularDet 二维数据仍使用原入口。

## Studio 与 Host 接下来处理

- 用新目录补齐六个标准参数分块，显示文件省略的默认项；用户编辑后再插入 `.par`，不批量展开默认值。新插入项与已有项使用同一套校验。
- 三轴 blocks 控件保持同级、位置稳定；0 关闭第二/第三轴，正整数（包括 1）启用；按状态展开范围/边界。第三轴依赖第二轴。非法输入保持原文并提示。
- 按 Core 坐标与单位显示标签。角度为 rad；普通未知字符串不能自动当作文件路径。
- Host 接通新命令、检查响应身份和版本；文件存在性、读写条件与输出目录预检由 Host 根据实际工作目录处理。目录标注了哪些字符串是路径。
- 编译输入跟踪增加 `src/core/StandardParameters.h`、`src/api/Configuration.h`、`src/api/Configuration.cpp`、`src/api/PresentationMetadata.cpp`、`src/api/LogCapture.h`。继续跟踪已有 RuntimeParams、ConfigParser、Preview、Response、main，以及策略注册/坐标定义依赖；新增枚举来自策略注册表，更新注册表后也需使旧 binary 失效。
- 左上角常驻模型名称和 `.par` 完整文件名。文件名前缀或目录差异作为配对疑点提示；不强制命名、同目录规则，也不据此禁止编辑。覆盖保存时明确目标文件；配对提示不能替代实际参数校验。
- 修复缩放时图形、坐标轴、标记和 Inspector 不一致；空间轴比例与场值比例分开设置，分别提供 Linear/Log；Advanced 提供范围和截断，另提供少量颜色表。显示设置不改写科学参数或响应数组。
- 切换模型时隔离旧 metadata、图形绑定与异步响应，关联 Source 与 Preview Model，并更新启动和能力说明。

13 项问题、交互要求与验收方法保存在 [Studio UI review](STUDIO_UI_REVIEW_2026-09-20.zh-CN.md)。这是需求记录；本轮完成的是上述 Core 支持，界面交互修复仍由 Studio 接入后验收。

## 验证

2026-09-20：Linux CPU Debug，GCC 13.3 / C++20，CUDA OFF、KLU OFF、OpenMP ON、BUILD_TESTING ON。构建成功。Helmholtz 测试使用仓库 LFS 指定的实际表，未提交数据表或构建产物。

以下八组 CTest 均通过：

| 测试 | 覆盖 |
|---|---|
| configuration_api_contract | 10 项；90 键覆盖、默认/别名、严格类型、选项、坐标与单位、未访问 EOS/设备/输出文件、输入及响应大小边界 |
| mainline_authority | 主线配置默认值、规范化、布尔及配置契约 |
| preview_initial_conversion | 与正式初始化共用的数据转换 |
| preview_api_contract | 原有 Sod 真实字段、状态、输入、错误和无文件输出；simulation oracle 保持默认跳过 |
| preview_parameter_reads | 来源读取记录与歧义 |
| preview_parameter_metadata | Sod x_pos 来源、位置绑定与真实字段一致 |
| preview_sampling_limits | 采样和响应边界 |
| preview_cellular_2d | 二维排列、直接 Init/EOS 数值对照、实际 Helmholtz 表、错误与取消 |

复现（已按 API README 配置 CPU 构建且准备实际 EOS 表）：

```sh
cmake --build build-studio-core-ui --target ARCH arch_mainline_authority \
  arch_preview_initial_conversion arch_preview_parameter_reads \
  arch_preview_sampling_limits arch_preview_cellular_reference -j 2
ctest --test-dir build-studio-core-ui --output-on-failure -j 2 \
  -R '^(configuration_api_contract|mainline_authority|preview_initial_conversion|preview_api_contract|preview_parameter_reads|preview_parameter_metadata|preview_sampling_limits|preview_cellular_2d)$'
```

本轮未进行 Studio UAT、完整 simulation 或 CUDA 验证。配置检查不加载 EOS，不执行模型 Setup，不认定 case 与文件配对正确，也不证明完整模拟可运行。任意 C++ 模型自动推断、Cellular 图形绑定及新的曲线坐标/三维 Preview 尚未增加。
