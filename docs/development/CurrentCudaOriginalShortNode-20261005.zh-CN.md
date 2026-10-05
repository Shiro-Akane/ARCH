# 完整 CUDA ARCH 与原 CPU-qualified 短子组

## 身份

源 HEAD 2a9840917837e77725fc1294ad9c187e518d762f，运行前后工作树干净。
唯一工作区 /home/arch/projects/ARCH-compute-optim；既有 build-cuda 的
CMAKE_HOME_DIRECTORY 相同，Release、CUDA ON、sm89。标准 cmake --build、
target ARCH、parallel 28、heavy pool 1；无独立 configure 或 main merge。

完整 ELF SHA256：
0203e0b5f097b465898fe68ad62dc734e2cecd680b3e50782682e70fffebe3fc。
构建95.438秒，peak owned RSS1848376KiB、swap增长0。
原CPU ELF未修改，匹配CPU receipt不重跑；不覆盖Studio managed Manifest。

## 验证

显式CUDA运行原 Campaign非均匀JeansWave64、BoxCampaign.run_checks(quick=True)
和RadialCampaign.run_checks(quick=True)，原输入/独立参考/阈值/低G迁移依据未改。
27记录PASS（Wave1、Box4、Radial22）：19实际科学进程、8预期拒绝。
19份gravity trace均device=1、kernels>0、residual<=原target；
Box沿原入口检查generation和有限Host上传，全部原repair检查events=0。

- JeansWave实际t=.1、密度RMS .0015957884745969439（原<=.02）；
  mass漂移3.597233444452286e-15（原<=1e-12），
  势能归一化能量漂移6.641700794284996e-6（原<=.01）。
- Gaussian静态云只覆盖t=0，不覆盖三档空间收敛/扩域；
  独立线性输运误差.00026142625776402555，burn/diffusion原gate通过。
- 球/柱dynamic AMR能量漂移1.20795e-5/2.57312e-5（原<5e-5），
  checkpoint续算逐位相等；refine/coarsen、near vacuum和八个配置拒绝通过。

原径向dynamic max_steps=12、refine/coarsen=40没有到tmax；
摘要逐run记录真实最后Plot时间，不以成功退出冒充统一终点。
hydrostatic只含quick两档，不宣称完整空间阶。
CPU/GPU相同不作独立物理参考，仍沿原analytic/Gauss/transport参考。

## 数据与未完成范围

[处理后summary](../../validation/gravity/results/cuda-original-short-20261005/summary.json)
记录ELF、源码聚合、原脚本/逐输入SHA、真实终点和Device标量。
NumPy/h5py既有venv，包装只调用原入口及核对trace；包装与raw H5/checkpoint/log、
全部输入fingerprints和ELF留本机studio/.local/integration/cuda-original-short-20261005。
运行29.067秒、peak RSS356620KiB、swap0，只是观察，不是冻结benchmark。

CUDA JENS公开配置/API/Driver gate未修改，完整冻结9演化+9restart的
本地门槛候选仍待明确授权。非均匀Wave不替代uniform-lifecycle-1。
RZ生产/科学gate保持；完整CUDA空间/时间/混合层级campaign、
finite-ring runtime force/work、RZ A→B→C→D科学出口及批准长跑/benchmark待完成。
不改变物理定义/阈值，不开展Windows适配。
