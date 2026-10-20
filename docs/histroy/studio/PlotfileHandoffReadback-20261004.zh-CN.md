# Plotfile 可复现对接读回

联合计划的独立 plt 小范围交付，Sod 1D 与 Cartesian CellularDet 2D。
修改前累积源码：73ef212266f9103ed8146a151a86626ed148cde3。
branch studio/compute-optim-integration；尚未 push，不能声称远端已能 fetch。
PlotIO.cpp 最近修改：b38f44a8d51a97e19a0d133a5ab547390d01f53d。
Reader FP64：5656f7b87abd075e464fbd48e1b98a82756949ba。
Unknown identity reasons：6b291494d5de8168e50ed57718a0a3cee7c3bd4c。
Core contract：23ff77c4f08419de2b3c5eadee214da2af25784e；直接读取 Git 对象，未 merge。

## 复现入口

使用已有 NumPy/h5py 环境：
python3 validation/io/export_plotfile_handoff.py --file /local/Sod.h5 --cell 11,15
python3 validation/io/export_plotfile_handoff.py --file /local/CellularDet.h5 --cell 19,15,15
python3 -m unittest discover -s tests/tooling/validation -p test_plotfile_handoff.py

脚本直接只读实际分散属性，不复制 H5，不创建候选 JSON HDF，不修改上游验证器。
输出文件 SHA、root/NativeGrid/SourceIdentity 属性、各字段 metadata/shape、
单元原值与 FP64 十六进制位模式、bounds、测度、中心和 block 标识。
详细处理后结果见同名 Summary.json；每字段仅一个单元，不导出完整数组。

## 实际结果

Sod shape=[12,16]，index=[11,15]，9 字段全部读回；文件 SHA 前后不变。
CellularDet shape=[20,16,16]，index=[19,15,15]，28 字段全部读回；文件 SHA 前后不变。
反例 4/4 PASS：非方形 C-order、不同 Morton 文件顺序、signed-zero 位模式、
只读不变、FP32 拒绝、partial/临时路径拒绝、外部链接与非法索引拒绝。
本轮不 Build、不 simulation、不重新生成科学输出，不重复不受影响的 Studio baseline。

这两份既有文件的 producer source 为 869ae3d3d30e8a9b16e377112af73e737b6c736a，
binary 为 f82bb7ff16c4acf54ae84970b9b403dce3d0370a468d241169519b3bfd1f6a44。
不能把当前 HEAD、新 writer reason 属性或当前 ELF 回填成其生产身份。

## 数组与语义

/Data/<field> FP64 [B,Nx]/[B,Ny,Nx]，i 最快，无 ghost。
flattened centers、lower/upper、measure 与文件字段同序；不得按 Morton 排序。
适配层组合 bounds 末两轴 axis/lower-upper、measure reshape 即可；
无需生产文件另存候选 JSON。ENTR 为 pressure-density proxy，单位 unknown，
不是热力学比熵。1D/2D 测度分别为每单位横截面积/每单位横向长度。

旧样本 build/effective/source 身份 unknown，没有逐项 reason；新 writer 已提供
reason 并保留 unknown。Adapter 必须保留版本区别，不自行补齐 known 身份。
complete 标记不证明 close/rename 成功，生产故障传播另有既有独立验证。
单元读回不证明全域无遗漏/重叠、科学误差或 producer freshness。
脚本为文件 SHA 扫描两遍，不是大型 Viewer 的低成本查询接口；
不能把逻辑 payload 当实际 HDF I/O。固定像素仍不限制首次叶块扫描。

原始 H5/plt/checkpoint/完整数组留本机，提交脚本、测试与处理后摘要。
下一步 Core owner 按此布局对接验证器并 review 科学语义；全模型原生 UAT、
科学待决、CUDA 与批准 O9 仍未完成。
