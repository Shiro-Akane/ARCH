# 通用 AMR 原生坐标与三维显示进度

2026-10-03，实施基线227b682e。沿联合计划推进全模型初态/AMR，不是科学演化验收或阶段封箱。

## 实现

- AMR mesh校验接受Core已发布的1/2/3D及Cartesian/Spherical/Cylindrical。
  三维/曲线坐标必须有native-grid metadata，维度、geometry、active轴与resources一致；
  workflow state.grid若存在也必须匹配，原complete/limited/none边界不变。
- 三维切面直接过滤原始叶块，不创造假的二维hierarchy。
  切面内部边界采用半开归属，最外侧面包含；投影与点击使用同一native轴映射。
- 匹配field时用相同field slice的native坐标叠加；无匹配field时独立显示mesh plane。
  独立切面仅改变显示状态，不编辑配置、不执行AMR/Preview。
- Inspector保留原始logicalKey、3轴bounds/cellShape/cellSpacing，每轴按Coremetadata显示单位。
  不把angular spacing标为cm，不从Init samples推断AMR cell values。
- field/AMR匹配继续要求project/case/config/build/binary/EOS source相同，并增加geometry/轴单位一致。
  单区状态没有空间曲线时，AMR在独立视图显示，不把它当作空间field overlay。

## 验证

最终Studio/Host243/243、lint/typecheck/production build/diff check PASS。
新增测试覆盖缺失或矛盾metadata/resources/state拒绝、三轴切面点击、
边界归属、隐藏level、原始对象不变、曲线units及field/mesh坐标不匹配拒绝。

实际build-cpu/bin/ARCH复用维护Gaussian输入，仅显式两个x3根块/不细化；
三种几何均返回ok、dim3、两个真实叶块。
当前TS workflow validator及三切面、外/内边界归属、原始geometry不变全部PASS。
这三项只证明根网格，不推广为三维混合细化。

另复用维护的Sod/Cellular有效v3输入，细化设置沿仓库local-workflow AMR参考输入：
Sod 10叶块，level0/1/2/3计数2/2/2/4；CellularDet20叶块，level1/2计数4/16。
都为ok/complete；当前TS校验、全部实际叶块中心点击与原始geometry不变PASS。
request/config SHA一致，独立cwd前后无输出，timeStepping=not_executed、
scientificOutput=not_created；没有运行simulation。
原始输入、JSON、stderr、全日志仅保留本机ignored目录，提交仅摘要。

## 尚未完成

生产binary及真实Host成功Build Manifest更新、真实HTTP多模型请求与Linux desktop UAT仍待执行。
当前证据不证明Canvas实际显示/交互已人工验收；不以adapter/unit tests替代桌面证据。
三维混合细化、全模型组合适用域尚须分层验证；有限mesh仍不能称complete。
独立plt、JENS/RZ、CUDA及科学预算/性能验收未完成，历史G/architecture审批独立保留。
本次不push/tag/main merge，不上传原始数组或科学输出。
