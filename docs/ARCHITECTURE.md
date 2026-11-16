# 架构说明

> 面向维护者。产品层设计（功能、界面、训练流程）见 [DESIGN.md](DESIGN.md)；本文只讲代码为什么这样组织。

## 1. 分层与依赖方向

```
        ┌──────────────────────────────────────────┐
        │  cli        figtools 可执行文件           │
        └───────────────────┬──────────────────────┘
                            │
        ┌───────────────────▼──────────────────────┐
        │  pipeline   Recipe · Executor · JSON      │
        └───────┬───────────────────────┬──────────┘
                │                       │
        ┌───────▼────────┐      ┌───────▼──────────┐
        │  ops  算子注册表│      │  io  PNG/BMP/PPM │
        └───────┬────────┘      └───────┬──────────┘
                │                       │
        ┌───────▼───────────────────────▼──────────┐
        │  core  Image · geometry · colour · hash   │
        └──────────────────────────────────────────┘
```

依赖只允许**向下**。`core` 不认识 `io`，`ops` 不认识 `pipeline`。这条规则让 `core` 可以独立复用到 Qt 层、Python worker 和插件里。

目标与依赖：

| target | 职责 | 依赖 |
|---|---|---|
| `ft_core` | 像素容器、几何、色彩原语、哈希、指标 | 无（仅 STL） |
| `ft_io` | 编解码与格式探测 | `ft_core`、zlib |
| `ft_ops` | 算子实现 + 注册表 + 参数规范化 | `ft_core` |
| `ft_json` | 零依赖 JSON 读写 | 无 |
| `ft_annotate` | 标注模型、COCO/YOLO 导出 | `ft_core` `ft_json` |
| `ft_dataset` | 扫描、划分、检查、快照导出 | `ft_core` `ft_io` `ft_annotate` `ft_json` |
| `ft_pipeline` | Recipe、执行与缓存键 | `ft_core` `ft_io` `ft_ops` `ft_json` |
| `figtools` | CLI | `ft_pipeline` |

## 2. 错误模型

不使用异常跨模块传播：

```cpp
Result<Image> resize(const ImageView&, int w, int h, Interp);
Result<Params> normalize(const OperatorSpec&, const Params&);
Status validate_recipe(const Recipe&, const Registry&);   // Result<void>
```

- `ErrorCode` 是可枚举的分类（参数非法 / 越界 / 不支持 / IO / 解析 / 内部），便于 GUI 决定「提示还是阻止」。
- `Result` 是 `[[nodiscard]]`，忽略返回值会编译告警。
- 断言只用于「程序员错误」（越界访问、重复注册），不用于用户输入。

## 3. 确定性契约

一次运行的结果由四元组决定：

```
cache_key = H( input_pixels , seed , recipe.version , Σ H(op_id, op.version, canonical_params) )
```

三条约束保证它成立：

1. **参数规范化**：`Registry::normalize()` 把参数补成算子声明的全集，填默认值、校验范围、统一类型。未声明的参数直接报错——不会出现「多传了一个参数但被静默忽略」。
2. **算子版本**：`OperatorSpec::version` 参与哈希。改变输出行为必须 +1，否则旧结果的 cache key 会与新结果相同。
3. **无隐藏状态**：算子不读环境变量、不读时钟、不用全局随机。需要随机就吃 `seed`。

测试 `execution_is_deterministic` 与 `cache_key_depends_on_parameters` 守着这条契约。

## 4. 算子模型

```cpp
struct ParamSpec { key, label, doc, unit, type, min, max, default, enum_values };
struct OperatorSpec { id, label, group, summary, version, geometry, params[] };
using OperatorFn = Result<Image>(const ImageView&, const Params&);
```

- **一处定义，四处复用**：CLI 帮助、GUI 控件、校验、缓存键，全部从 `ParamSpec` 派生。
- **显式注册**：`Registry::instance()` 首次调用时依次调用 `register_geometry_ops` / `register_color_ops` / `register_filter_ops`。不用静态初始化器，避免静态库链接时目标文件被丢弃。
- **几何算子带 `geometry=true`**：上层据此判断是否需要同步标注坐标、是否重建金字塔。

## 4.5 标注坐标空间

标注存的是**当时画布上那张结果图**的坐标（float，原图像素单位），并记录
`image_width / image_height`。这样"看到什么就标什么"成立，导出也不必反推缩放。

代价是：如果处理链改了输出尺寸（裁剪、缩放），已有标注与新画面不再对齐。
当前策略是**明确提示而不是静默重投影**——状态栏提示"输出尺寸已变化，已有标注仍基于
1200 × 800"。真正的解法是给每个标注记录它所属的资产版本，留到 v0.4 与数据集版本一起做。

导出：

- **COCO**：单图，`bbox` 用外接框，`area` 用真实面积（多边形为鞋带公式），
  `segmentation` 矩形写成 4 点环、多边形写成顶点序列。
- **YOLO**：类别用**类别列表下标**（不是 `id`），坐标按图像尺寸归一化；
  多边形退化为外接框（检测格式），分割训练需要 YOLO-seg 的额外格式，v0.4 补。

## 4.6 数据集划分：为什么不按组取整

第一版按"每个类别组各自 floor 分配"，在小组上直接崩坏：12 张图分 4 类，每组 1–2 张，
`floor(0.15 * 1) = 0`，验证集永远是 0。看起来只是"数据少"，实际是算法在小样本上失效。

现在用**在线取整**：遍历打乱后的样本，第 k 个样本分配给"目标份额 `ratio_i * k` 与实际
已分配数差距最大"的那个 split。

- 结束时每个 split 的计数与 `ratio * n` 的误差 ≤ 1，与样本量无关；
- 分层只需把样本按主类别排序后打乱块内顺序，算法自然把同类样本摊到三个集合；
- 没有"组太小就退化"的边界情况。

代价是不保证每个类别在每个 split 的**精确**配额——它保证全局比例准确、类别尽量分散。
真正的配额约束（每类在 val 里恰好 N 个）留给需要时再加，因为过度约束反而会在大数据集
上造成系统性偏移。

## 5. I/O 边界

- 解码器统一输出 RGBA8。灰度复制三通道，调色板展开，24 位 BMP 补 alpha=255。
- PNG 只支持 8 bit、非隔行、色彩类型 0/2/3/4/6。超出范围返回 `kUnsupported`，**不做有损降级**——静默降级比报错危险。
- 探测（`probe_image`）只读前 64 KB，用于列表展示万级资产时不触发完整解码。

## 6. 性能现状与已知瓶颈

实测（i5 级 CPU，1200×800，单线程）：

| 算子 | 耗时 | 说明 |
|---|---|---|
| `median` r=1 | 202 ms | 每像素每通道做 9 元素排序，**最大瓶颈** |
| `unsharp` | 36 ms | 含一次可分离高斯 |
| `gamma` | 34 ms | 逐像素 `pow` |
| `contrast` | 10 ms | 纯线性 |

优化方向（按收益排序）：

1. `median` 改用直方图滑动窗口，O(1) 均摊，预计 10× 以上。
2. `gamma` / `contrast` 预计算 256 项 LUT，避免逐像素浮点幂。
3. 行级并行（`std::execution` 或线程池），当前完全单线程。
4. 大图分块 + 瓦片缓存（对应 DESIGN.md §12.6）。

这些都属于 v0.2 的性能工作，接口不变。

## 7. 测试策略

`tests/` 零第三方依赖，69 项用例，跑完 < 1 秒：

| 文件 | 覆盖 |
|---|---|
| `test_core.cpp` | 像素存取、几何变换（旋转四次回原图、翻转自反）、HSL 往返、哈希敏感性、指标边界 |
| `test_ops.cpp` | 注册表完整性、参数规范化（默认值/未知参数/越界/非整数）、算子不变量（gamma=1 恒等、阈值只出 0/255） |
| `test_io.cpp` | 三种格式往返逐像素比对、探测、错误路径 |
| `test_pipeline.cpp` | 配方往返稳定、非法配方拒绝、执行确定性、禁用步骤跳过 |
| `test_annotate.cpp` | 面积/外接框/命中判定、COCO 与 YOLO 归一化、JSON 往返字节一致 |
| `test_batch.cpp` | 目录扫描与排序、递归、跳过/覆盖、取消、进度单调、批量输出与单张逐字节一致 |

原则：**测不变量，不测实现**。例如不测「median 的第 3 行第 7 列是 128」，而测「孤立噪点被消除」「输出尺寸不变」。

## 8. 与最终形态的衔接

设计基线里还有 Qt GUI、标注、数据集、训练 worker（见 DESIGN.md §9、§12）。接入方式已经预留：

- **GUI**：`ft_ops` 的 schema 直接生成参数面板；`ft_render` 复用 `ImageView` 做对比合成，不复制像素。
- **训练 worker**：`ft_data` 导出数据集快照，Python 侧只读；回传 ONNX + `model.json`，C++ 侧用 ORT 加载，一致性校验门槛 <1e-3。
- **插件**：纯 C ABI，`OperatorSpec` 用扁平结构体描述，避免 C++ ABI 不兼容。

## 9. 编码规范摘要

- C++20，`-Wall -Wextra -Wpedantic` 零告警；命名空间 `figtools`，内部实现放 `figtools::detail` 或匿名命名空间。
- 命名：类型 `CamelCase`，函数与变量 `snake_case`，私有成员 `trailing_underscore_`。
- 头文件自包含，`#pragma once` 不用，统一 include guard（`FIGTOOLS_<PATH>_HPP_`）。
- 单文件不超过 600 行；超过就说明职责没拆开。
