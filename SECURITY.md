# 安全策略

## 支持的版本

| 版本 | 安全修复 |
|---|---|
| 0.1.x | ✅ |
| < 0.1 | ❌ |

## 报告漏洞

**不要开公开 issue。** 请发邮件到 `security@figtools.local`（正式域名启用前用仓库维护者邮箱），或使用 GitHub 的 Private vulnerability reporting。

请在报告中包含：

1. 受影响版本与平台
2. 复现步骤或最小样例文件（**不要附真实业务图像**，用生成的合成图）
3. 影响评估：崩溃 / 越界读 / 越界写 / 信息泄露 / 拒绝服务
4. 若已知，给出修复建议

我们会在 **3 个工作日**内确认，**30 天**内给出修复或缓解方案；修复发布后经你同意再公开致谢。

## 解析不可信输入的已知风险

图像编解码器天然是攻击面。当前处理方式：

| 风险 | 现状 |
|---|---|
| 长度字段伪造 | PNG/BMP/PPM 解码器在读取前校验 `offset + length <= size`，越界即返回 `kParseError` |
| 解压炸弹 | PNG 的 `IDAT` 解压目标缓冲按 `width × height × channels` 精确预分配，`uncompress` 长度不符即拒绝 |
| 超大尺寸 | `width/height` 上限 20000，超出返回 `kUnsupported` |
| 整数溢出 | 尺寸计算使用 `std::size_t`，并在乘法前检查上限 |
| 递归深度 | JSON 解析器限制嵌套深度 64 层 |

尚未覆盖（已知限制，见 DESIGN.md 路线图）：TIFF/EXR/DICOM 解析、并行解码、GPU 路径。

## 依赖

| 依赖 | 版本 | 用途 |
|---|---|---|
| zlib | ≥ 1.2.11 | PNG 的 DEFLATE |

依赖漏洞通过 GitHub Dependabot 跟踪；升级遵循 `CHANGELOG.md` 的 `Security` 段落。
