# 贡献指南

感谢你对 Alice 的关注。以下是参与贡献的一些说明。

## 提交 Issue

Bug 报告请包含：
- 操作系统和 VS2022 版本
- 复现步骤
- 期望行为和实际行为
- 相关日志输出

功能建议请说明使用场景和预期效果。

## 提交 PR

1. Fork 仓库
2. 基于 `main` 创建特性分支
3. 提交改动，commit message 用中文，格式：`feat: 功能描述` / `fix: 修复描述` / `docs: 文档`
4. 确保编译通过 (Debug + Release)
5. 提交 PR，描述改了什么、为什么改

## 代码规范

- C++ 23, MSVC v143
- 命名: PascalCase 方法, snake_case 成员变量 (带下划线后缀)
- 所有公共接口加 `/// <summary>` XML 注释
- 提交前用 `.clang-format` 格式化 (VS2022 内置支持)
- 不要在 Core 里写业务逻辑，业务归 Mod

## 项目结构

- `AliceCore/` — 核心库，改动需谨慎
- `AliceServer/` — 入口，尽量保持精简
- `AliceSdk/` — C# SDK，和 HostBridge.h 字段顺序必须一致
- `mods/` — 示例 Mod，独立于 Core
- `docs/` — 文档

## 协议

贡献的代码默认采用 Apache 2.0 协议。
