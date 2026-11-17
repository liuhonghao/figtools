# Third-party notices / 第三方组件声明

figtools 本体使用 MIT 许可证。下列第三方组件按各自许可使用，边界如下。

## 随程序分发（bundled）

| 组件 | 许可 | 用途 |
|---|---|---|
| Qt 6 (Core / Gui / Widgets) | LGPL-3.0 | 桌面 UI 框架，以动态链接方式使用 |
| MinGW-w64 runtime | GPL-3.0 with exception | Windows 构建的运行库 |
| zlib | zlib license | PNG 编解码 |
| stb_image / stb_image_write (nothings/stb) | public domain / MIT | JPEG / WebP / TGA 编解码，两个头文件随源码分发 |

Qt 以动态链接形式使用，遵守 LGPL-3.0 的动态链接条款。若需静态链接，应另行取得 Qt 商业授权或提供完整的 LGPL 合规材料。

## 独立安装、不随程序分发（separately installed）

| 组件 | 许可 | 用途 | 边界 |
|---|---|---|---|
| PyTorch / torchvision | BSD-3-Clause | 训练与推理后端 | 用户自行安装，不在发布包内 |
| Ultralytics (YOLO) | **AGPL-3.0** | YOLO 训练 / 推理 | **仅作独立安装的训练组件，不链接、不随主程序分发**。商用部署时注意 AGPL 的网络使用条款；如需规避，可替换为其它许可的 YOLO 实现 |
| ONNX / ONNX Runtime | MIT | 模型导出（可选） | 用户按需安装 |
| RapidOCR (rapidocr-onnxruntime) | Apache-2.0 | OCR（可选） | 用户按需安装；未安装时 GUI 给出提示 |

**关键边界**：AGPL-3.0 的 Ultralytics 不进入 figtools 的发布包，也不作为库链接进 C++ 程序。它只在用户的 Python 环境里以独立进程运行。这一点是为了避免主程序被 AGPL 的传染性条款覆盖。

## 开发工具

- CMake、Ninja、GCC / MSVC：构建工具，不分发
- matplotlib：论文图表脚本依赖，用户按需安装（PSF-based license）

## 数据集

TTPLA 数据集（用户自行提供）权属归原作者，本项目仅提供格式转换脚本，不分发数据。
