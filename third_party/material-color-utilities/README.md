# material-color-utilities（C++ 子集）

- 上游：<https://github.com/material-foundation/material-color-utilities>
- 固定提交：`5b3618b16fdc3825e21d5679bafd144662088ea1`（2026-08-21）。`cpp/` 目录最后一次上游改动是 2024-10-01。
- 许可：Apache License 2.0，见 `LICENSE`。
- 范围：`cpp/` 下除 `*_test.cc` 以外的全部源码，目录结构保持不变（上游以 `cpp/...` 为包含路径根）。
- 用途：Cirrus UI 主题系统的封面取色与 MD3 配色方案（`ui/theme/`），只被 UI 链接。

## 本地修改

共五处修改，均在代码中以 `Cirrus:` 注释标出：

1. `cpp/quantize/wsmeans.cc`：为不引入 Abseil，`absl::flat_hash_map<Argb, int>` 改为 `std::unordered_map<Argb, int>`，删除对应 include。上游注释给出的耗时差异约 10.2 ms 对 8.0 ms（112×112 像素、128 色），在后台线程中可以接受。
2. `cpp/utils/utils.cc`：同上，`HexFromArgb` 由 `absl::StrCat(absl::Hex(argb))` 改为 `snprintf("%x")`，输出格式相同（小写、无前导零）。
3. `cpp/temperature/temperature_cache.h`：补上缺失的 `#include <optional>`（上游依赖其构建环境的传递包含，GCC 下无法编译）。
4. `cpp/cam/hct_solver.cc`、`cpp/utils/utils.cc`、`cpp/cam/viewing_conditions.h`：C 复合字面量 `(Vec3){...}`、`(ViewingConditions){...}` 改为 C++ 列表初始化 `Vec3{...}`，结果相同（MSVC 在 C++ 下拒绝前者，报 C4576）。
5. `cpp/score/score.h`：`ScoreOptions::fallback_color_argb` 的初值 `0xff4285f4` 超出 `int`，加 `static_cast<int>`，值相同（MSVC 报 C2397）。

`M_PI` 不在源码里改：`CMakeLists.txt` 在 MSVC 下为本库定义 `_USE_MATH_DEFINES`。

## 2025 规范的 C++ 移植（`cirrus/`）

上游 C++ 只有 2021 规范（`cpp/` 最后更新 2024-10-01）。`cirrus/` 是 Cirrus 从同一提交的 TypeScript 移植的 C++ 代码，命名空间 `material_color_utilities::ts`，同样适用 Apache License 2.0：

| 文件 | 移植自 |
| --- | --- |
| `cirrus/dynamic_scheme.*` | `typescript/dynamiccolor/dynamic_scheme.ts`（2021、2025 调色板规则）、`typescript/palettes/tonal_palette.ts` |
| `cirrus/dynamic_color.*` | `typescript/dynamiccolor/dynamic_color.ts`、`contrast_curve.ts`、`tone_delta_pair.ts` |
| `cirrus/color_spec.h`、`color_spec_2021.cc`、`color_spec_2025.cc` | `typescript/dynamiccolor/color_spec_2021.ts`、`color_spec_2025.ts` |
| `cirrus/temperature.*` | `typescript/temperature/temperature_cache.ts`、`typescript/dislike/dislike_analyzer.ts` |
| `cirrus/spec_helpers.h` | Cirrus 自写的小工具 |

- 只包含 2021 与 2025 规范（含 phone / watch 平台参数），不含 2026 规范与 CMF 变体。与 TS 相同，2025 规范只作用于 Neutral、TonalSpot、Expressive、Vibrant，其余变体回退到 2021。
- 继续使用上游 C++ 的 `Hct`、`Cam16`、对比度、量化、评分等基础算法；`TemperatureCache` 与 `DislikeAnalyzer` 改用移植版本，因为上游 C++ 版本与 TS 结果不一致。
- 每个方案的色调计算带缓存（TS 只缓存 HCT），复制出的方案缓存为空。
- **验证**：164 个种子（150 个伪随机 + 14 个边界色）× 9 个变体 × 明暗 × 5 档对比度 × 2 个规范，共 1,649,840 个颜色与同一提交编译的 TS 逐一比较，84 个（0.005%）不同，均为某一通道相差 1 的浮点舍入。Release 下生成一套 55 个角色约 0.8 ms。
- 上游 `cpp/dynamiccolor` 与 `cpp/scheme` 源码保留在仓库中用于上游跟踪，但 UI 使用 `cirrus/` 中的 TypeScript 移植，不将这两组上游 `.cc` 文件加入默认 `cirrus_mcu` target。其余实际依赖的上游基础算法继续编译。

## 更新方法

```bash
git clone --filter=blob:none --no-checkout https://github.com/material-foundation/material-color-utilities.git build/reference/mcu
git -C build/reference/mcu checkout --detach <新提交>
```

复制 `cpp/` 中非测试文件覆盖本目录，重新应用上述修改，更新本文的提交号，并重新运行 Gallery 主题页检查配色。
