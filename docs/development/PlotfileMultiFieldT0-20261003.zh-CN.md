# Plotfile 多字段 t=0 生产读取对照

## 精确构建身份

源码基线58fd77bf7eef24b154089fb381e4a5f60eeeb03f；
本轮真实输出仍使用ab23bad7e8bea93c3569618d1a6335312372cc30构建的CPU binary：
c294f0d1be009bdeca36174a00a28c6ff6ca48b11b499416f6556a2ecb6b4bec。
不是用当前source HEAD冒充binary来源；未重新configure或重链ARCH。
metadata修正仅构建了已有publication scoped target。

## 授权输入及实际数据

复用已记录且输入/binary SHA匹配的既有CPU t=0 Sod/CellularDet .par；
只改变plt_variables=DENS→ALL与out_dir新持久目录。
tmax=0/max_steps=-1、EOS/网格/AMR/组分/阈值均保持，没有演化。
新旧checkpoint time=0/step=0，全部rho、mom_u/v/w、eng、enuc_rate、X、rhoX位级差0，
按level/logical key对齐，不按Morton排序。

Sod：9字段，[12,16]，文件55376 bytes，
SHA9cc6ec2d5158478c4b974c46ddd8ca2ac9eb57e1ca93eb833c3d638e9abc9b8d。
CellularDet：28字段（包括19个实际组分），[20,16,16]，1599616 bytes，
SHAe7c784db39da4372b41716f570c91ffc6c8159c7cf09e72320822a1041efdc27。
DENS/ENER及species X逐单元对checkpoint位级差0；
速度按authoritative recover()的momentum/rho直接商对照位级差0，不加floor或容差。
ALL不强制输出当前模块不可用字段；burn关闭时ENUC未导出，3D VELZ不在本切片。

每个实际字段metadata/slice/global LOD/physical point经Host和client validator通过；
37个point原始值另经h5py读原始field/global index位级回查，差0。
旧completion unknown、renderEligible=false及partial身份均保持。
PRES/TEMP/ENTR/VORT/DIVV检查结构、有限值、metadata与读取链；
没有独立EOS/诊断科学参考通过声明，也不把相同读取当成物理正确。

## Core checker finding与最小修改

精确23ff77c4f的header validator拒绝旧ENTR meaning=pressure_density_gamma1_proxy，
其canonical为pressure_density_proxy，返回entropy错误。
本轮统一producer/test标识；P/rho^Gamma1计算、原始数组、unknown单位原因不变。
publication/checkpoint scoped 2/2通过；编译生成的ENTR单字段header通过原checker。
测试fixture还刻意含unknown-basis unregistered extra，完整fixture header仍被checker拒绝；
单字段检查不冒充完整fixture/数组适配通过。
新canonical生产binary/H5验证待下一轮；不改写本轮旧原始H5或掩盖failure finding。
Owner的bounds/measure布局适配及全域AMR无遗漏/重叠、可信完整身份验证仍是独立工作。

## 可复现工具

- validation/io/run_plotfile_fields_t0.py：核对既有输入/binary SHA，
  仅允许明确CPU t=0；只允许两个输出键变化，输出独立持久目录与失败记录。
  Guard测试验证tmax>0、max_steps cap、CUDA时从未启动binary或改写参考输入。
- validation/io/verify_plotfile_fields.py：原生key/shape/FP64、新旧checkpoint及导出场映射，
  optional --reader-summary再回查每个Host点选原始值；不输出完整数组。
- validation/io/verify_plotfile_reader.mjs：新增optional --all-fields，旧DENS入口保持。
  每个导出字段走slice/LOD/point真实读取与client validation，不是native UAT。

原始数据、full logs/arrays保存在studio/.local/integration/plotfile-multifield-t0-20261003，
具体编号/输入与输出SHA见Summary。提交仅脚本、处理后metadata/minmax/差异/点选指标。
源码未涉及Studio功能，之前311项回归不重复；工具实际运行、guard、2项受影响Core scoped与diff检查通过。
未push/tag/main merge/CUDA/Windows适配；完整科学和平台目标继续未完成。
