# 变更记录

格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### Planned

- Qt 6 桌面外壳与前后对比视图
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
