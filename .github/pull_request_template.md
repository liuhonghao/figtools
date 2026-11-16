## 做了什么

<!-- 一句话说明变更，以及为什么需要它 -->

## 关联

<!-- Closes #12 / Refs: #34 -->

## 变更类型

- [ ] feat 新功能
- [ ] fix 缺陷修复
- [ ] perf 性能优化
- [ ] refactor 重构（无行为变化）
- [ ] docs / build / ci / chore

## 验证

- [ ] `cmake --preset dev && cmake --build --preset dev && ctest --preset dev` 全绿
- [ ] 新增/修改的行为有对应测试
- [ ] 本地跑过 `figtools demo` 与 `figtools compare`，输出符合预期

<!-- 性能相关请贴前后对比 -->
<!--
before: steps 4 in 282.50 ms (median 202.20 ms)
after : steps 4 in  91.30 ms (median  18.40 ms)
-->

## 影响面

- 公共头文件是否变更：
- `OperatorSpec::version` 是否升版：
- 是否影响已保存的配方或缓存键：
- 是否需要更新 `CHANGELOG.md`：

## 截图 / 输出

<!-- 界面改动或像素结果变化请附前后对比 -->
