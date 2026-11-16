# 变更记录

格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [0.5.0] - 2026-11-16

### Changed

- **桌面外壳视觉整体重做**：暖纸色系（`#F0EADC` 窗底 / `#FBF8F2` 面板 / `#FFFCF6` 卡片）、
  近黑墨色文字、赤陶色（`#B4562A`）作为唯一强调色；去掉系统 GroupBox / CheckBox 默认外观。
- **顶栏自绘**：品牌区、打开 / 示例 / 导出、对比模式分段控件、适应与 1:1；图标由
  `QPainter` 绘制，三平台一致。
- **管线卡片**：序号 + 名称 + 拨动开关 + 动作菜单；数值右对齐等宽字体；停用步骤整体降级。
- **右栏**：扁平 section + 键值列表；直方图改为平方根压缩 + 9 bin 平滑并附 RGB 图例，
  避免大面积背景把其余分布压成一条线。
- **状态栏**：改用自有消息标签（`notify()`），不再让 `QStatusBar::showMessage` 覆盖左侧
  文件名与坐标信息。
- **示例图**：由金属划痕改为暖色标定靶（西门子星、12 级灰阶、12 色块、分辨率条、斜线块），
  既贴合暖色界面，也能真实体现降噪 / 对比度 / 锐化的差异。
- 默认标注类别改为 区域 / 目标 / 参考 / 忽略，颜色与主题一致。

### Added

- `gui/theme.{hpp,cpp}`：调色板、字号、QSS、自绘图标。
- `gui/widgets.{hpp,cpp}`：`KeyValueList`（键值读数）与 `ToggleSwitch`（带动画开关）。
- `MainWindow::prepareForCapture()`：截图前强制适应窗口，保证取景稳定。

## [0.4.0] - 2026-11-16

### Added

- **ft_dataset**：目录扫描（图片 + `.annotations.json` 副车）、`DatasetStats`、
  重复内容检测（文件内容哈希）、训练前检查（无标注 / 重复 / 类别缺失 / 验证集过小 /
  孤立标注）。
- **确定性三分划分**：`assign_split(train, val, test, seed, stratified)`，
  采用在线取整（online rounding），保证每个 split 的计数与比例误差 ≤ 1，且分层时
  各类别都被摊到三个集合；同一 seed 与同一文件列表必然得到同一划分。
- **快照导出**：`images/`、`annotations/`、`splits/{train,val,test}.txt`、
  `dataset.json`、`DATASET_CARD.md`（含统计与检查结论）。
- **界面「数据集」页**：选择目录、比例/分层/种子控件、划分结果、检查列表、导出快照。
- **标注页新增「存为数据集标注」**：直接写 `<image>.annotations.json`，与数据集页闭环。

### Fixed

- `Dataset::warnings()` 对空 `vector` 使用 `operator[]` 导致段错误，改为定长
  `std::array<int, 3>`。该缺陷由新增的导出测试捕获。

## [0.3.0] - 2026-11-16

### Added

- **ft_annotate**：`Shape`（矩形 / 多边形）、`LabelClass`、`AnnotationSet`；
  多边形面积用鞋带公式，点在多边形内用射线法，矩形与多边形统一走 `bounding_box()`。
- **标注序列化**：原生 JSON 往返（含类别色、图像尺寸），导出 **COCO**
  （`images` / `annotations` / `categories`，含 `segmentation` 与 `area`）与
  **YOLO**（`class cx cy w h` 归一化，多边形取外接框）。
- **界面标注页**：类别列表（色块）、选择/矩形/多边形三种工具、标注列表、
  选中后 Delete 删除、`Esc` 取消、双击或回车闭合多边形。
- **标注文件存取**与两种格式导出按钮；标注坐标以当前结果图尺寸为基准，
  管线改变输出尺寸时状态栏给出提示。
- `--demo-annotations` 启动参数，用示例图预置两个标注，便于演示与截图。

### Changed

- **拆分 ft_json**：JSON 读写从 `ft_pipeline` 独立为 `ft_json`，使标注模块
  不必依赖图像编解码。`figtools/json/json.hpp` 为新的公共头路径。

## [0.2.0] - 2026-11-16

### Added

- **Qt 6 桌面外壳**（`FIGTOOLS_BUILD_GUI=ON`）：左中右三栏，1400×900 默认尺寸。
- **前后对比画布**：分割（可拖动分界线）、并排（同缩放）、只看结果三种模式；
  滚轮缩放、拖动平移、`F` 适应窗口、`1` 实际像素。
- **schema 驱动的处理链**：算子菜单按分组生成；每个算子的参数控件由 `ParamSpec`
  自动创建（枚举→下拉、布尔→勾选、数值→滑块+数字框）；支持上移/下移/删除/单步禁用。
- **图像属性与直方图**：尺寸、通道、像素数、内存占用；RGB 三通道直方图；均值与锐度。
- **导出与配方**：`Ctrl+S` 按原始分辨率重跑处理链并导出；配方可保存/载入。
- `make_sample_scene()` 从 CLI 提到 `ft_core`，命令行与界面共用同一个示例场景。
- `scripts/build_gui.sh`：无 CMake 环境下的 MinGW 构建与运行库部署脚本。

### Changed

- CLI 的 `demo` 改用 `ft_core` 的示例场景实现，行为不变。

### Known limitations

- 界面预览运行在 ≤1400 px 的代理图上，导出才使用原始分辨率（导出前会提示耗时）。
- 无标注、无批量任务队列、无多文档标签页。

## [Unreleased]

### Planned

- 标注工作台与 COCO/YOLO 导出
- Python 训练 worker 与 ONNX 交接
- 批量任务中心与进度体系

## [0.1.0] - 2026-11-16

首个可运行版本：确定性图像处理内核 + 命令行工具。

### Added

- **core**：RGBA8 `Image` / `ImageView` / `MutableImageView`；`Result<T>` 无异常错误模型；
  FNV-1a 内容哈希；PSNR / SSIM（8×8 分块）/ MAE / 变化像素占比。
- **geometry**：`resize`（最近邻、双线性）、`crop`、`rotate90`、`flip`。
- **io**：PNG（zlib，8 bit，色彩类型 0/2/3/4/6，非隔行）、BMP（24/32 位）、PPM/PGM（8 bit）；
  头部级探测 `probe_image`，不解码像素即可读取尺寸。
- **ops**：算子注册表与 schema 驱动的参数规范化；19 个算子覆盖几何、色彩、滤波、分析四组。
- **pipeline**：零依赖 JSON 解析器（保留对象顺序、深度限制 64）、`Recipe` 往返序列化、
  执行器与 `cache key`（输入哈希 + 算子版本 + 规范化参数 + 种子）。
- **cli**：`info` / `ops` / `apply` / `compare` / `demo` / `version` 六个命令，
  `--json` 与 `--trace` 便于脚本与排查。
- **构建**：CMake ≥ 3.24 + presets（dev / release / vs2022）、`-Wall -Wextra -Wpedantic` 零告警、
  三平台 CI、`-Werror` 可选。
- **测试**：40 项单元测试，零第三方依赖，覆盖几何不变量、参数校验、编解码往返、
  执行确定性与错误路径。
- **文档**：设计基线（1700 行）、架构说明、ADR、贡献指南、安全策略。

### Known limitations

- 单线程执行；`median` 为逐像素排序，1200×800 约 200 ms，是当前最大瓶颈。
- 不支持 16 bit、隔行 PNG、EXR、TIFF、DICOM。
- 无 GUI、无批量任务队列、无插件 ABI（均在路线图中）。

[Unreleased]: https://github.com/liuhonghao/figtools/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/liuhonghao/figtools/releases/tag/v0.1.0
