---
id: avoid-wildcard-in-c-comments
trigger: "when writing C/C++ comment blocks containing file path patterns or glob-like syntax"
confidence: 0.8
domain: "debugging"
source: "session-observation"
scope: project
---

# Avoid */ Inside C Block Comments

## Action
Never write `*/` inside a `/** ... */` block comment, even when it looks like a glob pattern. The C preprocessor interprets the first `*/` as the end of the comment.

## Evidence
- Wrote `@details 验证 settings_get_*/settings_set_* 函数的正确性` in a test file
- The `*/` in `settings_get_*/settings_set_*` was interpreted as comment end
- Remaining text `settings_set_*` was parsed as code, causing 6 compilation errors with Chinese characters
- Fix: Rewrote comment to avoid `*/` sequence: `@details 验证 settings 的 getter/setter 函数正确性`

## Rule
- In `/* ... */` comments, NEVER write `*/` as literal text
- Use alternatives: `settings_get_* / settings_set_*` (space before /) or rephrase
- This is especially easy to miss when writing documentation for wildcard/glob patterns
