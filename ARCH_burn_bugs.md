# ARCH 燃烧路径（nuclear burn）代码审查记录

> **性质说明**：以下是通读 ARCH 燃烧模块源码时发现的代码级问题清单，供作者参考。
> 这些都是在 `feat(burn): implement nuclear burn...` 这次提交的代码里发现的——该功能显然仍在开发中（WIP），所以这更像是一份"接线检查表"，不是对成品的挑错。
> 所有结论都附了 `文件:行号` 和证据链，可逐条复现。行号基于 2026-07-12 从 GitHub 下载的 ZIP 快照。

---

## 一句话总结

**燃烧模块的零件（pynucastro 速率、ODE 求解器、算子分裂框架）都已就位，但主程序里的"接线"存在若干索引/维度不一致，导致燃烧实际上一步都不会执行。** 根因是一个贯穿始终的常量错误：网络实为 **16** 种核素，但代码多处硬编码为 **19**。

| # | 严重度 | 问题 | 位置 |
|---|---|---|---|
| 1 | 🔴 Blocker | 温度写入 `Y_ODE[19]` 但求解器从 `Y_ODE[18]` 读取 → 点火判据永远不满足 → 燃烧被静默跳过 | Driver.h:157 vs ode_be-nr.h:26 |
| 2 | 🔴 Blocker | `ODE_NEQ=19` 但只填了 `RHS[0..15]` → `RHS[16..18]` 是未初始化内存却参与牛顿更新 | NetAprox19.h:17 / .cpp:40 |
| 3 | 🟠 Major | Cellular 的组分注册顺序与网络内部顺序不一致 → 即使前两条修好，喂进网络的成分也是错位的 | Cellular.cpp:57 vs network_properties.H:71 |
| 4 | 🟠 Major | 核能生成率 `enuc` 算出后被丢弃，燃烧过程温度不演化 → 无自加热，爆轰无法自持 | ode_be-nr.h:67 |
| 5 | 🟡 Minor | 网络实为 16 种核素却命名 `aprox19`；Cellular 注册的 `fe54`/`n14` 网络里根本没有 | network_properties.H:7 |

---

## 根因：16 vs 19 的维度错配

三个地方对"有多少个核素"的认知彼此矛盾：

- `src/physics/network/aprox19/network_properties.H:7` → `constexpr int NumSpec = 16;`
  实际核素表（`spec_names`，第 71 行起）：`N, H1, He3, He4, C12, O16, Ne20, Mg24, Si28, S32, Ar36, Ca40, Ti44, Cr48, Fe52, Ni56` —— **确实是 16 种**。
  （注：真正的 aprox19 还有 `N14`、`Fe54` 以及独立的中子/质子，共 19 种。此处 `NetworkGenertor.py` 生成的是一个 16 种的缩减网络。）

- `src/numerics/burnsolver/NetAprox19.h:17-18`
  ```cpp
  static constexpr int ODE_NEQ = 19;      // ← 应为 NUM_SPECIES + 1 = 17
  static constexpr int NUM_SPECIES = 16;
  ```
  求解的 ODE 系统 = 16 个组分 + 1 个温度 = **17** 维才对，这里却硬编码成 19。

- `simulation/Cellular/Cellular.cpp:57-77` 注册了 **19** 个 species（`h1, he3, he4, c12, n14, o16, ne20, mg24, si28, s32, ar36, ca40, ti44, cr48, fe52, fe54, ni56, n, p`）。

这个 `ODE_NEQ=19` 是整条 bug 链的源头——它同时污染了温度索引、RHS 长度、雅可比维度和温度截断。

---

## Bug #1 🔴 —— 温度索引错位，燃烧被静默跳过（这是"为什么一步都没跑起来"的直接原因）

**证据链**（以 Cellular 算例为例，此时 SpeciesManager 的 `n_spec = 19`）：

1. `src/driver/Driver.h:146` —— `get_species_to_buffer(i, Y_ODE)` 把 19 个组分质量分数填入 `Y_ODE[0..18]`。
2. `src/driver/Driver.h:157` —— `Y_ODE[n_spec] = T;` 即温度被写入 **`Y_ODE[19]`**。
3. `src/numerics/burnsolver/ode_be-nr.h:26` —— 求解器入口的点火判据：
   ```cpp
   if (Y_ODE[NEQ - 1] < burn_cfg.burn_temp_min || rho < burn_cfg.burn_rho_min)
       return true;   // 直接返回，什么都不做
   ```
   其中 `NEQ = ODE_NEQ = 19`，所以它读的是 **`Y_ODE[18]`**。

**后果**：`Y_ODE[18]` 装的是第 19 个 species（`p`，质子）的质量分数 ≈ `1e-20`，而 `burn_temp_min = 1e6`（Cellular.par）。于是 `1e-20 < 1e6` 恒成立 → `integrate()` **对每个网格单元都立即返回，燃烧从不执行**。（低密度环境区更早在 Driver.h:141 就被 `burn_rho_min` 跳过，高密度 VN 区则栽在这里。）

**建议**：让 `ODE_NEQ = NUM_SPECIES + 1`（=17），温度存放在 `Y_ODE[NUM_SPECIES]`。这样 Driver 的 `Y_ODE[n_spec]` 与求解器的 `Y_ODE[NEQ-1]` 才会指向同一格——但前提是 `n_spec` 也等于 `NUM_SPECIES`（见 Bug #3）。

---

## Bug #2 🔴 —— `RHS[16..18]` 未初始化却参与牛顿迭代

**证据链**：

1. `NetAprox19.cpp:40-43` —— `eval_rhs` 只填充前 16 个：
   ```cpp
   for (int i = 0; i < NUM_SPECIES; ++i)   // 只到 15
       RHS[i] = ydot_arr(i + 1);
   ```
2. `ode_be-nr.h:31` —— `RHS` 是栈上未初始化数组 `double RHS[MAX_N];`（MAX_N=31）。
3. `ode_be-nr.h:75-84` —— 牛顿迭代按 `NEQ=19` 遍历，`RHS[16]`、`RHS[17]`、`RHS[18]` 全是脏内存：
   ```cpp
   for (int i = 0; i < NEQ; ++i)   // 到 18
       b[i] = Y_old[i] - Y_k[i] + dt * RHS[i];   // i=16,17,18 时 RHS[i] 是垃圾值
   ```

**后果**：即使修好 Bug #1，牛顿残差里也会混入 3 个未定义分量，破坏收敛性/正确性。同理 `eval_jacobian` 生成的雅可比只覆盖 16×16 的组分块，第 17–19 行列未定义。

**建议**：与 Bug #1 一起，把 `ODE_NEQ` 收敛到 `NUM_SPECIES(+1)`；牛顿循环、`RHS`、雅可比统一按同一维度走。

---

## Bug #3 🟠 —— 组分注册顺序与网络内部顺序不一致

**证据链**：

- 网络内部顺序（`network_properties.H:71`，0-based）：`[0]=N(中子), [1]=H1, [2]=He3, [3]=He4, [4]=C12, ...`
- Cellular 注册顺序（`Cellular.cpp:57`，0-based）：`[0]=h1, [1]=he3, [2]=he4, [3]=c12, [4]=n14, ...`
- `NetAprox19.cpp:28-30` 直接按下标搬运：`state.xn[i] = Y[i];`

**后果**：Cellular 把纯 C12 放在自己的索引 3（`Cellular.cpp:80`, `GetSpeciesID("c12")=3`），但网络认为索引 3 是 **He4**。于是"点燃纯碳"实际变成了"点燃纯氦"，物理完全错乱。此外 Cellular 注册的 `n14`(idx4)、`fe54`(idx15) 在网络里并不存在，多出来的 `n`/`p` 也无处对应。

**建议**：让 Cellular（及所有算例）严格按网络的 `spec_names` 顺序和数量注册 species，或提供一个"算例组分名 → 网络索引"的映射层，避免裸下标搬运。

---

## Bug #4 🟠 —— 核能未耦合回能量方程，燃烧过程温度冻结

**证据链**：

1. `ode_be-nr.h:67` —— `double enuc = 0.0;` 是牛顿循环内的局部变量。
2. `ode_be-nr.h:71` —— `eval_rhs(Y_k, rho, RHS, enuc);` 把核能生成率写进 `enuc`……
3. ……但此后 `enuc` **再没有被读取**；ODE 系统里也没有温度演化方程（`RHS` 只含组分导数）。温度在整个子步进过程中被 `enforce_temperature_bounds`（:101）钳在原值附近。
4. `Driver.h:170-174` —— 燃烧结束后 `T_new = Y_ODE[n_spec]` 取回的是**没被加热过的原温度**，再据此重算内能并覆盖 `eng[i]`。

**后果**：燃烧只改变组分、不释放热量（等温燃烧），且 Driver 的能量更新是"按新组分+旧温度重新赋值 `eng`"而非"在守恒前提下叠加核能"。对爆轰而言这是致命的——没有自加热，激波后的燃料不会被点燃，爆轰波无法自持传播。这也解释了为什么 ARCH 目前本质上还是"纯流体 + 惰性示踪组分"。

**建议**：在 ODE 向量里加入温度演化 `dT/dt = (enuc - Σ (∂e/∂Xₖ) dXₖ/dt) / cᵥ`（或等价的能量方程形式），或采用"组分演化 + 结合能差 → 内能增量"的一致耦合；并让 Driver 的能量更新走守恒叠加而非直接覆盖。这一条是"reactive flow"能否成立的核心，属于物理设计决策，建议作者自行拍板。

---

## Bug #5 🟡 —— 命名与实际网络不符

- `NumSpec=16` 的网络被命名为 `aprox19`（目录、类名、`network_name` 参数）。真正的 aprox19 是 19 种核素。
- Cellular 注册的 `n14`、`fe54` 在当前网络中不存在，`ener_gener_rate` / `actual_rhs` 也不会处理它们。

**后果**：不影响编译，但会持续误导使用者（包括未来的自己）对网络规模和覆盖反应的判断。**建议**要么把网络补齐到真正的 19 种，要么把命名统一改为实际规模（如 `aprox16` / `subch16`）。

---

## 附：这些问题不影响我们已完成的 GPU 内核验证

我们的 CUDA 燃烧内核基准（`~/ARCHcuda/burnbench/`）是**直接调用 pynucastro 生成的 `actual_rhs` / `actual_jac` 本体**，绕过了上述 Driver/求解器的接线层，并用受控的、组分顺序正确的初值（纯 C12/O16、温度独立传参）。因此：

- 100 万格点、CPU vs H100 结果一致到机器精度（最大相对误差 9.6e-14）、6.8× 加速——**这个结论成立且与上述 bug 无关**。
- 上述 bug 修复后，GPU 内核可以直接对接；届时燃烧-流体耦合跑通，再做端到端的 Cellular 爆轰对比。

**建议的修复顺序**：Bug #1 + #2（一起，让维度自洽）→ Bug #3（组分映射）→ 用一个已知解验证纯流体不受影响 → Bug #4（能量耦合，物理核心）→ Bug #5（清理命名）。

---

*审查人：Claude（协助 XU）· 日期：2026-07-12 · 基于 GitHub ZIP 快照*
