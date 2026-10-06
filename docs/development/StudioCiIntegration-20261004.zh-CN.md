# Studio／Host 既有 CI 接入

## 范围与身份

基线 6d8114384b4713c9a9e95778abce3610a2cf959d 加当前 workflow/report-checker patch。
这是联合计划第6节的覆盖与成本收束，不是 CPU 科学或整个联合交付封箱。
唯一现有 ci.yml 增加 Linux Studio/Host job，保持现有 trigger、Tooling/CPU 全 inventory，
required 依赖三项；未建立新 workflow、CUDA lane 或 Windows/package lane。
setup-node v7 官方远端精确SHA为820762786026740c76f36085b0efc47a31fe5020，
固定Node24.21.0；无 cache restore，Electron binary download disabled。

## 一次完整套件与失败语义

Node tests/*.test.ts 与 npm test 的文件集合一致，Host 已包含，不再执行重复 test:host。
build 内 tsc --noEmit 不再另跑；lint 与 build 各执行一次。
既有 check_ci_results.py 新增独立 --node-tap 模式，拒绝空、失败、取消、skip、TODO、
缺计数、重复计数、缺plan、bailout、plan/实际条目不一致。
CPU/driver-cuda 的 inventory/JUnit 检查保留；12相关测试全部PASS。
不改科学测试、数字预算或原始场值，不把新 gate 当作已通过科学检查。

## 实际净依赖验证与成本

当前已提交源码导出至 ignored 本机副本，起始没有 node_modules，npm ci 成功。
首轮只导出 studio 导致缺 src/api/examples 等仓库 fixture：330项/274pass/56fail。
完整错误保留，后续lint/build未执行，没有删测试、加skip或绕过失败。
补齐同一HEAD的完整tracked archive，保留净安装依赖后，334/334无skip/TODO/cancel全部PASS。
Node报告gate、lint、build（包含类型检查）PASS，actionlint1.7.12校验checksum及语法PASS。

| 本机步骤 | exit | wall seconds |
|---|---:|---:|
| install | 0 | 3.804 |
| test | 0 | 5.382 |
| report | 0 | 0.031 |
| lint | 0 | 6.592 |
| build | 0 | 3.797 |

本机时间只描述14700K/WSL这一副本的命令，不是GitHub runner耗时、冷网络成本或科学benchmark。
安装可消费本机npm下载缓存，但没有复用原工作树node_modules。
Vite已有 >500kB chunk警告保留；没有抬高warning limit或为本步改造产品。

## 发布和验收边界

自动审批拒绝原拟新增raw日志artifact上传，理由为潜在路径等信息缺少外部披露授权。
安全替代采用不新增Studio artifact上传；原有Tooling/CPU规则未修改。
本机日志、净依赖副本和dist保持ignored，只提交处理后成本/失败摘要与脚本。
没有push或workflow dispatch；实际hosted CI结果仍未知。
既有architecture audit两项边界问题和科学失败/待决项不能被新Studio job覆盖成绿色。
完整CPU/CUDA、O7科学review、冻结O9及native输入验收仍按原计划未闭合。
