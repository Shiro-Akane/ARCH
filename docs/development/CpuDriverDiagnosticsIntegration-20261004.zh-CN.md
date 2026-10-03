# 主 CPU ARCH：诊断补丁生产集成

## 构建身份

clean源码07d4bf2486c3c26774768212fb6009e7e0be6f1f。
既有build-cpu CPU Release，cmake --build --target ARCH -j8；
未独立configure、未迁移build tree。
仅DriverIO.cpp编译、solver archive和主ELF链接三步，9.745s。
旧binary f82bb7ff16c4acf54ae84970b9b403dce3d0370a468d241169519b3bfd1f6a44；
新binary 60892079fba9785c3691e2594fee2cd4aa4379cf9777637b2dd66a51c66328b5，
7400552 bytes。时间是构建证据，不是物理benchmark。

## 生产回归

沿用既有3C固定网格Sod原输入，OMP1，
0.05/step67续算至0.2/step280；只更改输出路径与对应新checkpoint路径。
validation/io/verify_sod_restart_output_identity.py已在新主binary执行PASS：
连续/续算最终21个checkpoint dataset一致，其中20个数值dataset原bytes一致；
四个终点Plotfield一致，输入checkpoint不变，两个独立output-session UUID。

独立h5py补查新旧构建全部8份Plotfile共32个字段数组原bits一致。
正常连续/续算的Cartesian state_repairs.txt与旧构建原bytes一致。
这些比较证明诊断补丁没有改变既有用例数值，不是独立科学oracle。

## 真实 CLI 失败传播

另外沿用同一tmax0.2输入，预先在新ignored out_dir下将state_repairs.txt
链接到Linux /dev/full。主ARCH仍到达科学终点0.2/step280并写出checkpoint；
结束时诊断flush失败，stderr明确cannot flush state repair diagnostics，
实际CLI exit code=1，不能当作成功Run。

此证据不是实际磁盘耗尽，不保证已写出科学输出回滚、fsync或断电持久性。
报告原始失败/正常输出及日志均保存在本机ignored目录，
只提交处理后身份/指标。复现失败时应使用新本地目录；
不要把真实项目state_repairs.txt替换为symlink。

## 状态

上一份RZDriverRepairDiagnostics报告的“主ARCH尚未重编”为当时历史状态；
本轮已完成上述clean提交的主CPU集成。RZ特定标签与失败恢复仍由上一轮
实际Driver双profile fixture证明，本轮公开CLI仍为Cartesian Sod。
不声称公共RZ已开放或完整演化通过，未触碰其它Studio Project binary/Manifest。
纯IO诊断源码变动仅运行相关构建、Run/Restart数值与错误传播检查，
不重复配置/API/Studio无关baseline。diff check通过，无push/tag。
