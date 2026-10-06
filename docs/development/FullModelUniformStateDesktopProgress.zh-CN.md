# BurnOneZone 非空间初态原生桌面验收

基线 7426b51687123565a059129bf68bb00d50cfed46。仅 Linux/WSL production
Initial Condition，未启动燃烧时间演化、未生成科学输出、未改科学 Core。

## 实际 UAT 与修复

从真实 Linux arch-studio 入口打开已有 BurnOneZone.par（Helmholtz/aprox13），
独立窗口显示 Core coordinates.representation=uniform-state 的均匀状态表，
没有伪造空间曲线或热图。六字段及单位与该次真实响应相符。
第一次 Inspect initial state 操作发现 Inspector 仍显示 x=97656.25 与
raw spatial index；这些是采样协议坐标，非空间模型显示它们会误导使用者。

RealInitWorkspace 的 Inspector 按既有 authoritative representation 选择标题、
提示与来源标签。uniform-state 不显示 raw coordinates、inactive coordinates或
x1-fastest index；保留六字段原值及 case/config/build/binary 身份。空间模型分支
保留原显示逻辑。没有修改响应数组、.par、采样或EOS/物理计算。

修复后249/249 Studio/Host、lint、typecheck、production build、diff-check PASS；
包括新增plt五项测试，不另加Host子集制造重复计数。production新资源重新启动后，
原生 Inspect initial state 已显示 Initial state Inspector /
Uniform initial state · no spatial sample coordinate，无坐标。
同一输入重新打开后的六字段值、data digest、disk config digest和build/binary
身份逐项一致。原生 Update Preview 再次返回 current，同一warm session sequence=2；
本次观察tableLoads=0、tableHits=2、EOS conversions=1、exact-state reuses=511，
只作为资源复用证据，不当作冻结benchmark或burn性能验收。

两轮窗口正常关闭均exit0；Electron/Host PID分别269056/269102与273609/273656，
关闭前实际记录owned preview-session PID274084、parent273656；关闭后所有这些
/proc实体不存在，选中production binary的preview-session/preview-amr扫描为空。
首次短后台启动PID269032已实际消失、日志为空，未计为UAT；随后采用可追踪前台
session。没有根据窗口枚举为空就重复启动仍存活进程。

## 身份、原值和限制

详见 FullModelUniformStateDesktopProgress.Summary.json。完整local诊断留
studio/.local/integration/full-model-production-20261003/native-burn-*；
不提交完整响应数组或原始科学输出。EOS表取自既有本地输入，没复制/修改表。

该证据只覆盖非空间初态表/Inspector、warm更新、身份不变与正常关闭清理；
不是燃烧演化、点火、守恒、CUDA、完整全模型桌面矩阵或native AMR验收。
完整依赖freshness仍unknown，不借成功preview冒称全部依赖已新鲜。
手动Update Preview未改配置时，生成中旧label仍短暂显示Parameters changed；
这是已有状态文案过宽，已记录，未把它当作配置真实变化或本步顺手扩大修复。

未重编 ARCH、不push/tag/main merge、不开展Windows适配。
