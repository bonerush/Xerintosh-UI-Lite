---
id: large-scale-refactoring-workflow
trigger: "when performing multi-file refactoring across 10+ files"
confidence: 0.9
domain: "workflow"
source: "session-observation"
scope: project
---

# Large-Scale Refactoring Workflow

## Action
When refactoring across many files (10+), follow this workflow:

1. **Audit first**: Use sub-agents to explore codebase in parallel (dead code, style violations, duplicate patterns)
2. **Plan phases**: Group changes into risk levels (safe → medium → high)
3. **Commit checkpoint**: Commit current state BEFORE starting refactoring
4. **Batch by pattern**: Do all changes of the same type together (e.g., all renames, then all signature changes)
5. **Verify after each batch**: Compile + test after each logical batch
6. **Commit per batch**: One commit per logical batch for easy rollback
7. **Use sub-agents for mechanical renames**: 10+ file renames are perfect for delegation

## Evidence
- Started with 4 parallel exploration sub-agents (330+ tool calls) to understand full scope
- Organized into 3 risk phases: safe (dead code) → medium (patterns) → high (signatures)
- Committed before each major change — enabled clean rollback when `kern_log_get_level` removal broke tests
- Used sub-agent for shell_* → kern_shell_* rename (15 files, 160 replacements) — completed in one delegation
- Used sub-agent for device prefix rename (5 files, 44 replacements) — completed in one delegation

## Sub-Agent Prompt Template for Renames
```
I need to rename symbols in the project. For each symbol:
1. Use Grep to find ALL occurrences across src/ and test/
2. Use Edit with replace_all=true on each file
3. Verify no old names remain
```

## Rule
- Always audit before acting — the initial exploration found issues we wouldn't have expected
- Commit checkpoints are essential — we had to restore `kern_log_get_level` after removing it
- Delegate mechanical work to sub-agents — they handle 10+ file renames reliably
- Verify both native and hardware builds after each batch
