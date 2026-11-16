# 贡献指南

## 分支与发布

| 分支 | 用途 | 保护 |
|---|---|---|
| `main` | 始终可发布 | 禁止直推，必须 PR + CI 绿 |
| `feat/<scope>-<slug>` | 功能 | — |
| `fix/<scope>-<slug>` | 修复 | — |
| `chore/<slug>` | 构建、依赖、文档 | — |

版本遵循 [语义化版本](https://semver.org/lang/zh-CN/)：`MAJOR.MINOR.PATCH`。
`v0.x` 阶段允许破坏性变更，但必须在 `CHANGELOG.md` 的 `Changed` 段落写明迁移方式。

发布流程：

1. 更新 `CHANGELOG.md` 与 `CMakeLists.txt` 里的 `project(VERSION)`。
2. 打 tag `v0.1.0`，CI 产出三平台构建。
3. `gh release create v0.1.0 --notes-from-tag`。

## 提交规范

[Conventional Commits](https://www.conventionalcommits.org/)，一行标题 + 空行 + 正文。

```
feat(ops): 增加 Otsu 自动阈值算子

- 直方图类间方差最大化，O(256)
- schema 声明 invert 参数，默认 false
- 补充 test_ops 用例：输出仅含 0/255

Refs: #12
```

允许的 type：`feat` `fix` `perf` `refactor` `test` `docs` `build` `ci` `chore` `revert`。
scope 用模块名：`core` `io` `ops` `pipeline` `cli` `docs`。

**一个提交只做一件事**。重构与行为变更不要混在一个提交里，否则 review 无法判断哪一行改变了输出。

## 代码规范

- 格式化：`clang-format`（配置在 `.clang-format`，基于 Google，列宽 100）。
- 静态检查：`clang-tidy`（`.clang-tidy`）。
- 编译：`-Wall -Wextra -Wpedantic` 必须零告警；CI 在 `dev` preset 下按 `-Werror` 构建。

硬性规则：

1. **不跨模块抛异常**。可失败操作返回 `Result<T>`。
2. **不原地修改输入**。算子接收 `ImageView`，返回新 `Image`。
3. **不引入隐藏状态**。算子不得读时钟、环境变量、全局随机；随机必须来自 `seed`。
4. **公共头文件只依赖 STL**。第三方库留在 `.cpp` 里，否则依赖会沿着 include 扩散。
5. **新增算子必须声明 schema**，包括单位、范围、默认值与一句话说明；没有 schema 的算子不允许合并。
6. **改变输出行为必须升 `OperatorSpec::version`**，否则缓存键会串。

命名：类型 `CamelCase`、函数与变量 `snake_case`、私有成员 `trailing_underscore_`、常量 `kPascalCase`。
头文件用 include guard，格式 `FIGTOOLS_<PATH>_HPP_`。

## 测试要求

- 每个行为变更都要有测试，且测试要能在无网络、无 GPU、无样图的环境跑。
- 测**不变量**而不是实现细节：尺寸、通道数、幂等性、值域、往返一致性、失败码。
- 涉及像素比较时用 `hash_image()` 或逐像素阈值，不要用截图。
- 性能相关改动在 PR 描述里贴前后耗时（`figtools apply ... --trace`）。

本地一键：

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

## 评审清单

提交者自查：

- [ ] 标题与提交信息符合规范
- [ ] 新增/修改的逻辑有测试，`ctest` 全绿
- [ ] 公共头文件变更同步了文档
- [ ] 没有提交构建产物、IDE 配置、临时图片
- [ ] 破坏性变更已写进 `CHANGELOG.md`

评审者关注：

- [ ] 边界条件：空图、1×1、超大尺寸、越界矩形
- [ ] 错误路径：失败是否带上下文（哪一步、哪个参数）
- [ ] 确定性：是否引入了未受 `seed` 控制的状态
- [ ] 复杂度：是否有 O(n²) 或每像素分配
- [ ] 命名与分层：是否把 `io` 的东西漏进了 `core`

## 报告问题

用 issue 模板，附上：

1. `figtools version`
2. 复现命令（含配方 JSON）
3. 期望行为与实际行为
4. 若涉及像素差异，附 `figtools compare` 输出
