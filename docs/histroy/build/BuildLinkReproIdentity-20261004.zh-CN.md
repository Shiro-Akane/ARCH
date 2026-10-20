# Linker 原生 repro 输入身份审计

基线807e512eda79bd986daaff414089afef86618370；只运行独立双文件C++ LTO link。
ARCH 未配置/构建/执行，probe ELF亦未运行。未更改生产flags、版本或既有Manifest。

本机mold2.30.0帮助称 --repro embed .repro section；真实产物为probe.repro.tar，
probe ELF无.repro section。最初objcopy退出0但提示无法dump且没有生成文件，
后续读取保护性失败；检查实际目录/readelf后改读原生archive，未重跑该link。

原项目长路径与独立/tmp短路径两次link均成功。
两份archive各21个member，其中19份实际输入全部命名
probe.repro/probe.repro.tar，而且有19个不同SHA/内容长度。
不能用archive位置、长度、猜测顺序或近似对象补全depfile路径绑定。
短路径仍复现，因此未把结果仅归因为工作目录长度。

只读工具validation/io/audit_link_repro.py不提取或执行archive，
按64MiB/256member限制流式读取并枚举原member SHA，
两份真实archive均INVALID/exit1。唯一member名也只能UNVERIFIED，
不能据此自行称dependency closure已完整。

当前mold原生repro方式不能作为可靠完整输入捕获方案。
dependenciesComplete继续false，full freshness unknown。
完整修复仍需真实linker消费期间的路径/字节/生命周期证据、
与depfile及持久配置输入闭包关联；本轮未替换/升级工具链或禁用LTO来凑通过。
原archive/ELF/depfile/log留本机ignored和摘要记录的/tmp探测目录；只提交处理后证据。
