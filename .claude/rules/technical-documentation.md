# Technical Documentation Standards

> This rule governs how technical documentation is created, structured, and maintained for this project. All documentation work must follow these standards to ensure consistency, maintainability, and navigability.

## Purpose

Technical documentation for this project serves as a knowledge base for both current development and future maintenance. It must be:
- **Clear and intuitive**: Every document must be understandable by readers with varying levels of expertise.
- **Deeply linked**: Code references must point directly to source files and line numbers.
- **Navigable**: Documents must form a connected web (like Obsidian) with a central index/map.
- **Comprehensive**: Coverage must reach 99% of the project's logic, architecture, and design decisions.
- **Living**: Documentation must evolve with the code and incorporate insights from every user conversation.
- **Written in Chinese**: All technical documentation must be written in **Chinese (中文)**. Code identifiers, file paths, and external references remain in their original language, but all explanatory prose, headings, comments, and analogies must be in Chinese.

---

## 1. Code Reference Linking

### MANDATORY: Source Links for Every Code Block

Every code snippet in a document **must** be accompanied by a clickable link pointing to the exact source file and, where possible, the specific line range.

**Format:**

```markdown
*📄 Source: [filename.cpp](../path/to/file.cpp#L10-L25)*

```cpp
// code here
```
```

**Rules:**
- Use **relative paths** from the document's location to the source file.
- Append **line number anchors** (`#L10-L25`) when the code corresponds to a specific block.
- If the snippet is simplified for teaching, label it as **"Teaching Example"** instead of a source link:

```markdown
*📄 Teaching Example (simplified for clarity)*

```cpp
// simplified code
```
```

**Why:** This allows readers to jump directly from the document to the source in their IDE, eliminating ambiguity about where a snippet originates.

### MANDATORY: Chinese Pseudocode for Complex Logic

When explaining a block of code logic to the user, **always accompany the source code with a Chinese pseudocode breakdown** (中文伪代码拆解).

**Format:**

```markdown
### 中文伪代码拆解

```
函数 类名::函数名(参数) {

    // 用中文描述每一步在做什么
    变量 = 初始值

    遍历所有子项 {
        // 第一步：设置目标位置
        子项.目标x = ...
        子项.目标y = ...

        // 第二步：处理特殊情况
        if (某个条件) {
            子项.当前位置 = 最终位置   // 直接到位
            continue
        }

        // 第三步：处理动画效果
        if (开启动画) {
            子项.当前位置 = 屏幕外面    // 从屏幕外滑入
        }
    }
}
```

**Rules:**
- Use **Chinese variable names** that describe what the variable does (e.g., `子页面` instead of `_iter`, `索引` instead of `_index`).
- Add **section comments** (like `// 第一步：...`) to group related logic.
- Include a **one-sentence summary** at the end explaining the core idea.
- Keep the pseudocode **high-level** — don't translate line-by-line, translate intent-by-intent.

**Why:** Pseudocode bridges the gap between raw C++ syntax and human intuition. It helps beginners understand "what is the code trying to do" before they understand "how does each symbol work".

---

## 2. Macro-First: The Central Index ("Map")

### MANDATORY: Central Index Document

Before writing any sub-topic document, you **must** create and maintain a **central index** document that acts as the "map" of the entire codebase.

**File:** `doc/index.md` (or `doc/README.md`)

**Contents:**
1. **High-level Architecture Diagram**: A visual or textual tree showing the top-level modules, classes, and their relationships.
2. **Navigation Map**: A structured list of all documents in the system, organized hierarchically.
3. **Quick Links**: Direct links to the most frequently referenced files (e.g., main entry point, config, core classes).

**Example Structure:**

```markdown
# Project Knowledge Map

## Architecture Overview
```
Project Root
├── Core
│   ├── UI Layer
│   │   ├── [Item System](item-system.md) ← link to sub-doc
│   │   └── [Launcher](launcher.md)
│   └── HAL Layer
│       └── [OLED Driver](oled-driver.md)
└── Config
    └── [Build & Runtime Config](config.md)
```

## Document Tree
- **[Item System](item-system.md)**
  - [Base Item](item-base.md)
  - [Menu & Pages](menu-pages.md)
  - [Widgets](widgets.md)
```

### MANDATORY: Synchronize on Every Addition

Whenever a new technical document is created or an existing one is significantly restructured:
1. **Update the central index** immediately to include the new document in the correct hierarchy.
2. **Add bidirectional links**: The new document must link back to its parent topic in the index, and the index must link to the new document.

**Why:** Without a central map, documentation fragments into an unnavigable pile of files. The index ensures a reader can always orient themselves.

---

## 3. Multi-File Document System (Obsidian-Style)

### MANDATORY: No Monolithic Files

Technical documentation **must not** be a single large file. Instead, it must be a **network of small, focused documents** connected by bidirectional links.

### Document Granularity

| Guideline | Rule |
|---|---|
| **Max file size** | 400 lines. If a document exceeds this, split it into sub-topics. |
| **One concern per file** | Each document should cover exactly one logical topic (e.g., "The Animation System", "The Camera Class"). |
| **Depth of hierarchy** | Prefer 2-3 levels. If you find yourself at level 4, reconsider the structure. |

### Bidirectional Linking

Every document **must** link to related documents in both directions:

- **Up-links**: Link to the parent/index document.
- **Down-links**: Link to child or related sub-documents.
- **Cross-links**: Link to peer documents that are frequently referenced together.

**Format:**

```markdown
<!-- At the top of every document -->
> **Parent:** [Item System](item-system.md) | **Prev:** [Base Item](item-base.md) | **Next:** [Widgets](widgets.md)

<!-- Within the body, when referencing another topic -->
For how coordinates are transformed, see [Camera & Viewport](camera-viewport.md).
```

**Why:** This creates a navigable web. A reader exploring one topic can naturally discover related topics without returning to a single table of contents.

---

## 4. Maintainability and Extensibility

### MANDATORY: Append to the Correct Leaf

When adding new content, you **must** insert it into the **most specific (deepest) existing document** that covers that topic.

**Workflow:**
1. Check the central index to find the relevant topic.
2. If a sub-document exists for that specific topic, append/update there.
3. **Only** create a new file if the topic genuinely does not fit into any existing document.
4. If a new file is created, update the central index and add bidirectional links immediately.

### Document Templates

Every new document **should** follow this template:

```markdown
# Topic Title

> **Parent:** [Parent Topic](parent.md) | **Related:** [Peer](peer.md)

## Overview

1-2 sentences explaining what this document covers and why it matters.

## Key Concepts

### Concept A
Brief explanation.

*📄 Source: [file.cpp](../path/file.cpp#L10-L20)*
```cpp
// code snippet
```

### Concept B
...

## Relationship to Other Components

- Links to related documents.
- Diagrams or descriptions of how this fits into the larger system.

---

> **See Also:** [Sub-topic A](sub-a.md) | [Sub-topic B](sub-b.md)
```

**Why:** Templates ensure every document has a predictable structure, making it easier for readers to find what they need and for writers to maintain consistency.

---

## 5. Coverage and Conversation Integration

### MANDATORY: 99% Code Coverage in Docs

Technical documentation must strive to explain **99%** of the project's meaningful logic, architecture, and design decisions.

**What "coverage" means:**
- Every public class and its primary methods are documented somewhere.
- Every non-trivial design decision (e.g., "Why use a Camera class?") is explained.
- Every complex algorithm (e.g., animation easing) is broken down.
- Configuration options and their effects are listed.

**What it does NOT mean:**
- Every single line of code needs a comment (that's what source links are for).
- Boilerplate or auto-generated code needs explanation unless it is modified.

### MANDATORY: Integrate User Conversations

After every user conversation that yields new insights, clarifications, or explanations about the codebase:

1. **Identify the relevant document** in the technical doc system.
2. **Synthesize the new information** and insert it into the appropriate section.
3. **Do not copy-paste chat logs.** Distill the insight into clean, structured prose.
4. **Update the central index** if the conversation revealed a gap in the documentation structure.

**Example:**

> User asks: "Why does `Camera` use negative coordinates?"
>
> After explaining, the assistant must update `doc/camera-viewport.md` to include a "Why Negative Coordinates?" section that synthesizes this insight for future readers.

---

## 6. Quality Maintenance and Refactoring

### MANDATORY: Periodic Consolidation

After **every 3-5 significant edits** to the documentation (or after a major feature is fully documented), a **consolidation pass** must be performed.

**Consolidation Checklist:**

- [ ] **Duplicate Removal**: Scan for redundant explanations across documents. Merge or deduplicate.
- [ ] **Link Verification**: Check that all internal links still point to valid files. Fix broken paths.
- [ ] **Index Sync**: Ensure the central index accurately reflects the current document tree.
- [ ] **Readability**: Review documents for clarity. Simplify complex sentences. Ensure analogies are still accurate.
- [ ] **Source Link Accuracy**: Verify that source file links and line numbers are still correct after code changes.
- [ ] **Depth Check**: Ensure no document has grown beyond 400 lines. Split if necessary.

### Tone and Style

- **All text must be in Chinese (中文)**: Every heading, paragraph, explanation, analogy, and comment must be written in Chinese. Only code identifiers, file paths, and external references (e.g., GitHub URLs) may remain in their original language.
- **Be concise**: Prefer short, direct sentences.
- **Use analogies for complex concepts**, but always pair them with the technical reality.
- **Avoid jargon** without explanation. If you must use a term like "polymorphism," briefly define it.
- **Use active voice**: "The Camera updates its position" rather than "The position is updated by the Camera."

---

## 7. Directory Structure and Location

### MANDATORY: Single Root Location (`doc/`)

All technical documentation files and their associated subdirectories **must** reside under the project's root `doc/` folder. No documentation files are allowed to be scattered in other locations (e.g., alongside source files, in `.claude/`, or in personal notes).

**Required structure:**

```
project-root/
├── doc/
│   ├── index.md                 # Central index / knowledge map (see §2)
│   ├── README.md                # (Optional) Human-friendly project overview
│   ├── architecture/
│   │   ├── overview.md
│   │   └── data-flow.md
│   ├── ui/
│   │   ├── item-system.md
│   │   ├── menu-pages.md
│   │   └── widgets.md
│   ├── hal/
│   │   └── oled-driver.md
│   └── assets/
│       └── diagrams/
├── Core/
├── Drivers/
└── ...
```

### MANDATORY: Mirror Source Structure

The directory tree inside `doc/` **should** mirror the project's source code structure as closely as possible. This makes it trivial for a developer to find the documentation that corresponds to a given module.

**Example mapping:**

| Source Path | Documentation Path |
|---|---|
| `Core/Src/Xerintosh/ui/item/` | `doc/ui/item-system/` |
| `Core/Src/Xerintosh/config/` | `doc/config/` |
| `Core/Src/hal/hal_dreamCore/` | `doc/hal/dreamcore.md` |

**Why:** When a developer is reading `Core/Src/Xerintosh/ui/item/menu.cpp`, they should intuitively know to look in `doc/ui/item-system/` for the explanation.

### MANDATORY: Naming Conventions

- **Files**: Use `kebab-case.md` (e.g., `item-system.md`, `camera-viewport.md`).
- **Directories**: Use `kebab-case` (e.g., `item-system/`, `architecture/`).
- **Index files**: Each subdirectory **should** contain an `index.md` or `README.md` that explains the contents of that folder and links to its child documents.
- **No spaces or special characters** in filenames.
- **No version numbers or dates** in filenames (e.g., `design-v2.md`, `api-2024-01.md`). Use Git for versioning.

### MANDATORY: Asset Organization

Images, diagrams, and other static assets referenced by documents must be stored in a dedicated `doc/assets/` directory, organized by topic:

```
doc/
├── assets/
│   ├── images/
│   │   ├── architecture-diagram.png
│   │   └── ui-state-machine.png
│   └── diagrams/
│       └── class-inheritance.puml
```

**Why:** Separating assets from text keeps the document tree clean and makes bulk operations (e.g., moving a topic) easier.

---

## Summary Checklist

Before considering any documentation task complete, verify:

- [ ] Every code block has a source link (or is labeled as a teaching example).
- [ ] The central index exists and is updated with the new document.
- [ ] The new document links back to its parent and relevant peers.
- [ ] The document follows the single-topic-per-file rule (< 400 lines).
- [ ] Insights from user conversations have been synthesized and inserted.
- [ ] No redundant content exists elsewhere in the doc system.
- [ ] **The file is located inside `doc/` and follows the directory naming conventions.**
- [ ] **The file path mirrors the source code structure it documents.**
- [ ] **Any new subdirectory has an `index.md` explaining its contents.**
- [ ] **All prose, headings, explanations, and analogies are written in Chinese (中文).**
