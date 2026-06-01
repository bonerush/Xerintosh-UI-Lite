---
id: tdd-for-c-refactoring
trigger: "when adding new APIs or refactoring C code with existing test suite"
confidence: 0.85
domain: "testing"
source: "session-observation"
scope: project
---

# TDD Workflow for C Refactoring with PlatformIO

## Action
When refactoring C/C++ embedded code with PlatformIO + GoogleTest, follow this TDD cycle:

1. **RED**: Write test that exercises the new API → verify it FAILS to compile (undefined reference)
2. **GREEN**: Implement the minimal code → verify test PASSES
3. **Verify**: Run full test suite (`pio test -e native`) + hardware build (`pio run -e m5stick-c`)
4. **Commit**: One commit per TDD cycle with clear message

## PlatformIO-Specific Notes
- `pio run -e native` only builds the main program, NOT tests
- `pio test -e native` compiles AND runs tests
- After adding new test files, may need `rm -rf .pio/build/native` to force re-scan
- Use `--gtest_filter="TestSuite.*"` to run specific test suites
- The native binary at `.pio/build/native/program` can be run directly for faster iteration

## Evidence
- Added 16 settings getter/setter tests → verified RED (undeclared identifier errors) → implemented → verified GREEN (16/16 pass)
- Used `--gtest_filter="SettingsAccessorTest.*"` for fast iteration during development
- Hardware build verification caught no issues but is essential for embedded projects

## Rule
- Always verify RED before implementing — confirms the test is actually testing the right thing
- Always run both native AND hardware builds — native tests may pass but hardware build may fail due to platform-specific code
- Clean build cache when new test files aren't being picked up
