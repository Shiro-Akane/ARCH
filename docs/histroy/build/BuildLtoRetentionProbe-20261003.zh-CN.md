# LTO 输入保留：独立工程实验

2026-10-03，基线9681891f；准确命令/哈希/覆盖见Summary。
微型输入是 main.cpp: int helper(); int main(){return helper()==3?0:1;}
与 helper.cpp: int helper(){return 3;}。先分别以-O3 -flto=auto -c编译为
持久main.o/helper.o，再分别链接普通与-plugin-opt=-debug两组。
未运行任何可执行文件，未编译/运行ARCH。

外层ld.mold wrapper在mold返回后调用现有fingerprintLinkDependencies，
仍缺临时ltrans对象；该失败候选未接入生产。
mold --help提到repro section，但实际生成旁置repro.repro.tar；
21成员未含generated ltrans。objcopy退出0但没有section文件，不算成功。
单命令源码编译/链接加plugin debug可留ltrans，但driver临时源码对象仍删除；
不能把这种样本宣称完整。调整为与真实ARCH类似的持久object输入后，
19个linker记录全部可fingerprint，包含depfile中的真实ltrans路径/大小/SHA。
控制组与保留组完整ELF及.text逐位一致，只证明本次微型样本。

Application.cmake新增默认OFF ARCH_RETAIN_LTO_LINK_INPUTS。
仅GNU C++且emit-link-dependencies=ON允许；否则明确失败。
开启时给linker现有plugin添加-debug诊断保留参数；不关闭IPO、不改FP/科学flags。
当前Studio profile未开启，build cache/Manifest/binary均保持。
真实ARCH仍97缺失、dependenciesComplete=false/freshness-unknown。

下一步需在Host持有的持久TMPDIR中启用，并实际验证ARCH完整link输入、
磁盘预算、编译/外部CMake输入覆盖及before/after身份；
该选项存在不等于完成capture或证明clean build，不能据此更改旧Manifest。
CPU工程身份完整后才继续冻结科学输入与CUDA；不改变科学预算。
本地probe二进制/archives/完整日志均ignored，仅提交精简指标和源码说明。
本轮不push/tag/main merge，不新增科学输出。

默认关闭/显式开启/缺emit三种CMake guard使用独立微型fixture验证，
直接包含Application.cmake的实际option块；默认link命令无debug，
开启命令含debug，缺emit配置明确失败。未configure正式ARCH。
修改Application.cmake属于tracked build input变化；后续Host应要求Build，
不能因为选项默认关闭就保留旧“输入未变”的声明。
