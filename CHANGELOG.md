# 变更记录

格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [1.1.0] - 2026-11-16

### Added

- **YOLO 检测链路**（`train_yolo.py` / `detect_yolo.py` / `ft_train/yolo_data.py`）：
  把数据集快照转成 YOLO 布局（images/labels + data.yaml，类别 id 重映射），
  用 Ultralytics 训练，模型包含 `best.pt`、尽力导出的 `weights.onnx` 与 C++ 可读元数据；
  推理输出与普通检测器同构，画布无需改动。**Ultralytics 是 AGPL-3.0，仅作独立安装的
  训练组件，不随主程序分发**。本机 conda 环境尚未安装，脚本会给出明确 error 事件。
- 训练对话框新增第三种任务「YOLO 目标检测（推荐）」及对应参数默认值（640px/50轮/批16）；
  YOLO 的 mAP50/mAP50-95 事件与分类的 accuracy 事件统一到同一进度/日志界面。

### Changed

- **工作台 UI 整体重做**（参考 LiDAR360 / 沄视云析的工作台范式，保留暖白配色）：
  - 顶栏 + **Ribbon 功能区**取代旧的文字工具栏：6 个页签、大图标按钮按组排列。
  - 左侧新增**项目树**（图像/标注/数据集）与工作页分层；中央新增**视图工具条**；
    右侧属性区加统一**标题条**；底部新增**输出坞**，`notify()` 的消息带时间戳落盘显示。
  - 画布背景改为温和深灰（看图更准），面板去圆角、统一 2px 直角与明确分区线，
    全局配色/字号/图标集中在 `theme.*`，新增 12 个图标。
  - 训练/批量对话框压缩间距、日志区加标题条，去掉"网页表单"的松散感。

## [1.0.0] - 2026-11-16

### Added

- **目标检测闭环**：标注框/多边形第一次真正参与训练。
  - `ft_train/patches.py`：从数据集快照的标注裁出正负 patch（含随机抖动与背景负样本），
    保留原 train/val/test 划分，写出 `patches.json`。
  - `train_detect.py`：训练一个含 `__background__` 的 patch 分类器，
    模型包带 `"kind":"detector"` / `positive_classes` / `patch_size`。
  - `detect.py`：多尺度滑窗 + 批量分类 + NMS（高 IoU 或中心接近或包含关系均抑制），
    输出 `detections`（label/confidence/x/y/w/h）。
- **界面**：训练对话框新增「任务」下拉（分类 / 检测）与对应默认参数；
  模型菜单新增「检测当前图像」（`Ctrl+D`）；`loadModel` 识别模型类型，
  分类器与检测器互不误用；画布新增独立检测覆盖层（赤陶色框 + 标签）。
- `smoke_test.py` 增加检测训练 + 推理断言。

### Fixed

- 检测状态栏在 `std::move` 后读取 `size()` 导致永远显示“0 个目标”。
- 多尺度滑窗对同一目标产生多个框：NMS 增加中心距离与包含关系判据。

## [0.9.0] - 2026-11-16

### Added

- **批量推理**（`python/predict_batch.py`）：一个进程分类整个目录，按批送模型，
  输出 CSV（UTF-8 BOM，含 Top-K 标签与分数）或 JSONL；阈值以下标 `unknown`；
  stdout 事件为 `phase` / `dataset` / `batch` / `done` / `error`。
- **批量推理对话框**（`Ctrl+Shift+R`）：模型/输入/结果文件、Top-K、批大小、设备、
  递归开关、Python 解释器；进度、逐批日志、取消与“打开结果”。
- `smoke_test.py` 覆盖批量推理：训练后跑整目录并断言 CSV 行数。

### Changed

- 训练与批量推理对话框的“包含子目录”改用拨动开关；日志首行不再打印完整长路径，
  并关闭列表的水平滚动条。

## [0.8.0] - 2026-11-16

### Added

- **Python worker**（`python/`）：`train.py` 训练分类器、`predict.py` 单图推理，
  通过 stdout 的 NDJSON 事件与 C++ 通信，日志走 stderr。已在本机
  Python 3.10 / torch 2.11（CUDA 可用）验证。
- 支持两种数据布局：ImageFolder 类文件夹，以及数据集页导出的快照
  （`images/ annotations/ splits/ dataset.json`，按标注主类别取标签）。
- 三种架构：`resnet18`、`mobilenet_v3_small`、`smallcnn`；AdamW + 余弦退火，
  按验证精度保留最佳权重；训练集/验证集/测试集分别评估，种子固定可复现。
- **模型包**：`model.pt`、`model.json`（C++ 可读元数据）、`metrics.json`（逐轮曲线）、
  `job.json`（完整训练配置）。
- **界面**：`Ctrl+T` 训练对话框（解释器、架构、轮数、批大小、学习率、设备），
  实时轮次进度与日志；`Ctrl+R` 对当前图像推理，结果写入右栏“模型”区并在画布上显示标签。
- 自动探测带 torch 的 conda 环境；`FIGTOOLS_PYTHON` 与设置项可覆盖。
- `python/smoke_test.py`：生成两类小图 → 训练 → 推理 → 断言，端到端冒烟，本机通过。

### Changed

- 构建脚本与 CMake 把 `python/` 复制到可执行文件旁，运行时不依赖源码树位置。

## [0.7.0] - 2026-11-16

### Added

- **RecipeHistory**（`ft_pipeline`）：配方快照式撤销栈。提交以规范化 JSON 判等，
  重复状态不产生条目；`coalesce_token` 让同一次参数拖动只占一步；
  撤销后再提交会丢弃重做分支；容量上限默认 64，超出丢弃最旧条目。
- **界面撤销/重做**：编辑菜单（`Ctrl+Z` / `Ctrl+Y` 或 `Ctrl+Shift+Z`）与顶栏图标按钮，
  按钮与菜单文案随可用状态更新（“撤销 调整 对比度 · 强度”）。
  添加 / 删除 / 上移下移 / 启停 / 参数调整 / 载入配方 / 清空管线全部入栈。
- 参数编辑器按 `param:<step>:<key>` 合并连续拖动，滑块一次拖到底只算一步；
  松开滑块或结束数字编辑时 `seal()` 封口，下一次拖动另起一步。
- 空管线时的“清空管线”不入栈，避免产生无意义历史。

### Changed

- `MainWindow` 现在以 `recipe_` 为唯一真源，历史条目只是快照；撤销后整体重建处理链，
  避免控件状态与配方不一致。

## [0.6.0] - 2026-11-16

### Added

- **ft_batch**：`list_images()` 按扩展名收集并排序；`run_batch()` 串行套用配方，
  逐文件记录耗时与错误；`batch_output_path()` 保留子目录结构。
  取消在文件之间轮询，进行中的文件一定会写完，不会留下半个输出。
- **CLI `batch`**：`batch <in-dir> <out-dir> -r <recipe> [--suffix] [--format]
  [--recursive] [--overwrite] [--json]`，失败文件单独列出且不影响其余文件，
  有失败时退出码为 1。
- **批量处理对话框**（`Ctrl+B`）：输入/输出目录、后缀、格式、递归、覆盖；
  实时匹配计数、进度条、逐文件日志、取消与“打开输出目录”。
  工作在 `std::thread` 上，UI 通过队列调用接收进度，不阻塞重绘。
- **窗口状态**：`QSettings` 记住窗口几何与上一次打开目录。
- 8 项批量测试：排序、递归、跳过、覆盖、取消、进度单调、输出与单张执行逐字节一致。

### Changed

- `apply` 与 `batch` 共用同一个配方读取函数，去掉重复的文件读取逻辑。

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
