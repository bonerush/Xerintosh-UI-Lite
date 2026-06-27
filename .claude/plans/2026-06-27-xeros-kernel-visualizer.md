# Xeros Kernel Visualizer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a zero-dependency static web visualizer (`doc/superpowers/visualizer/`) that animates Xeros kernel flows step-by-step for learning, teaching, and debugging assistance.

**Architecture:** A single-page HTML/CSS/JS app with a three-column layout (module navigation, SVG animation stage + step details, register panel). Scenes are JSON-driven; a JavaScript renderer applies CSS classes and `stroke-dashoffset` animations to pre-drawn SVG nodes/arrows based on the current step.

**Tech Stack:** Pure HTML5, CSS3, ES6 modules. No build tools, no CDN, no frameworks.

---

## File Structure

```
doc/superpowers/visualizer/
├── index.html              # App shell + layout
├── css/
│   ├── base.css            # Layout, typography, color tokens (暗夜实验室)
│   ├── nodes.css           # SVG node states and register panel styles
│   └── animations.css      # Arrow flow, step transitions, progress bar
├── js/
│   ├── app.js              # Bootstraps app, wiring navigation + player
│   ├── scene-renderer.js   # Renders scene SVG and applies step classes
│   ├── step-player.js      # Playback, manual stepping, keyboard shortcuts
│   └── scenes/
│       ├── index.js        # Exports all scene modules
│       ├── boot.js
│       ├── context-switch.js
│       ├── scheduler.js
│       ├── smp-ipi.js
│       ├── sync.js
│       ├── vfs.js
│       ├── interrupt.js
│       ├── memory.js
│       ├── registers.js
│       └── shell.js
└── assets/
    └── svg-templates/      # Reusable SVG symbol definitions (optional)
```

---

### Task 1: Create directory scaffold

**Files:**
- Create: `doc/superpowers/visualizer/index.html`
- Create: `doc/superpowers/visualizer/css/base.css`
- Create: `doc/superpowers/visualizer/css/nodes.css`
- Create: `doc/superpowers/visualizer/css/animations.css`
- Create: `doc/superpowers/visualizer/js/app.js`
- Create: `doc/superpowers/visualizer/js/scene-renderer.js`
- Create: `doc/superpowers/visualizer/js/step-player.js`
- Create: `doc/superpowers/visualizer/js/scenes/index.js`
- Create: `doc/superpowers/visualizer/js/scenes/boot.js`
- Create: `doc/superpowers/visualizer/js/scenes/context-switch.js`
- Create: `doc/superpowers/visualizer/js/scenes/scheduler.js`
- Create: `doc/superpowers/visualizer/js/scenes/smp-ipi.js`
- Create: `doc/superpowers/visualizer/js/scenes/sync.js`
- Create: `doc/superpowers/visualizer/js/scenes/vfs.js`
- Create: `doc/superpowers/visualizer/js/scenes/interrupt.js`
- Create: `doc/superpowers/visualizer/js/scenes/memory.js`
- Create: `doc/superpowers/visualizer/js/scenes/registers.js`
- Create: `doc/superpowers/visualizer/js/scenes/shell.js`

- [ ] **Step 1: Create directories and empty files**

Run:
```bash
mkdir -p doc/superpowers/visualizer/css \
         doc/superpowers/visualizer/js/scenes \
         doc/superpowers/visualizer/assets/svg-templates
touch doc/superpowers/visualizer/index.html \
      doc/superpowers/visualizer/css/base.css \
      doc/superpowers/visualizer/css/nodes.css \
      doc/superpowers/visualizer/css/animations.css \
      doc/superpowers/visualizer/js/app.js \
      doc/superpowers/visualizer/js/scene-renderer.js \
      doc/superpowers/visualizer/js/step-player.js \
      doc/superpowers/visualizer/js/scenes/index.js \
      doc/superpowers/visualizer/js/scenes/{boot,context-switch,scheduler,smp-ipi,sync,vfs,interrupt,memory,registers,shell}.js
```

Expected: Directories and empty files exist.

- [ ] **Step 2: Commit scaffold**

```bash
git add doc/superpowers/visualizer
git commit -m "chore(visualizer): create directory scaffold"
```

---

### Task 2: Implement base layout and color tokens

**Files:**
- Modify: `doc/superpowers/visualizer/css/base.css`
- Modify: `doc/superpowers/visualizer/index.html`

- [ ] **Step 1: Write CSS variables and global reset**

In `doc/superpowers/visualizer/css/base.css`:

```css
:root {
  --bg-root: #121214;
  --bg-panel: #18181b;
  --bg-stage: #18181b;
  --border: #27272a;
  --text-primary: #fafaf9;
  --text-secondary: #e8e6e1;
  --text-muted: #a1a1aa;
  --text-dim: #71717a;
  --accent: #fafaf9;
  --radius: 10px;
  --font-ui: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  --font-mono: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
}

* { box-sizing: border-box; }

html, body {
  margin: 0;
  padding: 0;
  height: 100%;
  background: var(--bg-root);
  color: var(--text-secondary);
  font-family: var(--font-ui);
  font-size: 14px;
  overflow: hidden;
}

.app {
  display: grid;
  grid-template-rows: 48px 1fr 44px;
  height: 100vh;
}
```

- [ ] **Step 2: Write header, sidebar, stage, detail, register panel layout**

Append to `doc/superpowers/visualizer/css/base.css`:

```css
.header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 1.25rem;
  background: var(--bg-panel);
  border-bottom: 1px solid var(--border);
}

.header-title {
  color: var(--text-primary);
  font-weight: 500;
  letter-spacing: -0.01em;
  font-size: 1rem;
}

.header-meta {
  font-size: 0.75rem;
  color: var(--text-dim);
}

.main {
  display: grid;
  grid-template-columns: 200px 1fr 180px;
  min-height: 0;
}

.sidebar {
  border-right: 1px solid var(--border);
  padding: 1.25rem;
  background: var(--bg-panel);
  overflow-y: auto;
}

.sidebar-title {
  font-size: 0.65rem;
  color: var(--text-dim);
  letter-spacing: 0.08em;
  text-transform: uppercase;
  margin-bottom: 1rem;
}

.module-list {
  list-style: none;
  margin: 0;
  padding: 0;
}

.module-item {
  padding: 0.55rem 0.75rem;
  margin-bottom: 0.4rem;
  border-radius: var(--radius);
  color: var(--text-muted);
  font-size: 0.85rem;
  cursor: pointer;
  transition: background 0.15s ease, color 0.15s ease;
}

.module-item:hover {
  background: rgba(250, 250, 249, 0.05);
  color: var(--text-secondary);
}

.module-item.active {
  background: var(--accent);
  color: #18181b;
  font-weight: 500;
}

.center {
  display: grid;
  grid-template-rows: 1fr auto;
  min-height: 0;
  padding: 1.25rem;
  gap: 1rem;
}

.stage {
  border: 1px solid var(--border);
  border-radius: var(--radius);
  background: var(--bg-stage);
  position: relative;
  min-height: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: hidden;
}

.stage-label {
  position: absolute;
  top: 0.75rem;
  left: 1rem;
  font-size: 0.65rem;
  color: var(--text-dim);
  letter-spacing: 0.05em;
}

.details {
  border: 1px solid var(--border);
  border-radius: var(--radius);
  background: var(--bg-panel);
  padding: 1rem;
}

.details-step {
  font-size: 0.65rem;
  color: var(--text-dim);
  letter-spacing: 0.05em;
  margin-bottom: 0.6rem;
}

.details-title {
  font-size: 0.95rem;
  color: var(--text-secondary);
  margin-bottom: 0.5rem;
  line-height: 1.5;
}

.details-desc {
  font-size: 0.9rem;
  color: var(--text-muted);
  line-height: 1.6;
  margin-bottom: 0.5rem;
}

.details-source {
  font-size: 0.78rem;
  color: var(--text-dim);
}

.details-source a {
  color: var(--text-muted);
  text-decoration: none;
}

.details-source a:hover {
  color: var(--text-primary);
  text-decoration: underline;
}

.registers {
  border-left: 1px solid var(--border);
  padding: 1.25rem;
  background: var(--bg-panel);
  overflow-y: auto;
}

.registers-title {
  font-size: 0.65rem;
  color: var(--text-dim);
  letter-spacing: 0.08em;
  text-transform: uppercase;
  margin-bottom: 1rem;
}

.register-table {
  font-family: var(--font-mono);
  font-size: 0.75rem;
  line-height: 1.8;
  color: var(--text-muted);
}

.register-row {
  display: flex;
  justify-content: space-between;
}

.register-row.changed .register-value {
  color: var(--text-primary);
  font-weight: 500;
}

.controls {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 1.25rem;
  background: var(--bg-panel);
  border-top: 1px solid var(--border);
}

.control-group {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

button {
  background: transparent;
  border: 1px solid var(--border);
  color: var(--text-muted);
  border-radius: 6px;
  padding: 0.4rem 0.75rem;
  font-size: 0.8rem;
  cursor: pointer;
  transition: all 0.15s ease;
}

button:hover {
  border-color: var(--text-muted);
  color: var(--text-secondary);
}

button.primary {
  background: var(--accent);
  color: #18181b;
  border-color: var(--accent);
  font-weight: 500;
}

button.primary:hover {
  background: #e8e6e1;
}

.progress-track {
  flex: 1;
  height: 4px;
  background: var(--border);
  border-radius: 2px;
  margin: 0 1rem;
  overflow: hidden;
}

.progress-fill {
  height: 100%;
  background: var(--accent);
  width: 0%;
  transition: width 0.25s ease;
}
```

- [ ] **Step 3: Write index.html shell**

In `doc/superpowers/visualizer/index.html`:

```html
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Xeros Kernel Visualizer</title>
  <link rel="stylesheet" href="css/base.css">
  <link rel="stylesheet" href="css/nodes.css">
  <link rel="stylesheet" href="css/animations.css">
</head>
<body>
  <div class="app">
    <header class="header">
      <span class="header-title">Xeros Kernel Visualizer</span>
      <span class="header-meta">v3.0.0 · Core0 · Core1</span>
    </header>

    <div class="main">
      <nav class="sidebar">
        <div class="sidebar-title">Modules</div>
        <ul class="module-list" id="module-list"></ul>
      </nav>

      <section class="center">
        <div class="stage" id="stage">
          <div class="stage-label" id="stage-label">Select a module</div>
          <svg id="stage-svg" width="100%" height="100%" viewBox="0 0 800 400"></svg>
        </div>
        <div class="details" id="details">
          <div class="details-step" id="details-step"></div>
          <div class="details-title" id="details-title"></div>
          <div class="details-desc" id="details-desc"></div>
          <div class="details-source" id="details-source"></div>
        </div>
      </section>

      <aside class="registers">
        <div class="registers-title">Registers</div>
        <div class="register-table" id="register-table"></div>
      </aside>
    </div>

    <footer class="controls">
      <div class="control-group">
        <button id="btn-prev">◀ 上一步</button>
        <button id="btn-next">下一步 ▶</button>
      </div>
      <div class="progress-track">
        <div class="progress-fill" id="progress-fill"></div>
      </div>
      <div class="control-group">
        <button id="btn-play" class="primary">播放</button>
        <button id="btn-reset">重置</button>
        <select id="speed">
          <option value="2000">慢</option>
          <option value="1000" selected>中</option>
          <option value="500">快</option>
        </select>
      </div>
    </footer>
  </div>

  <script type="module" src="js/app.js"></script>
</body>
</html>
```

- [ ] **Step 4: Verify layout renders**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Expected: Browser opens with dark three-column layout. No JS errors in console yet.

- [ ] **Step 5: Commit**

```bash
git add doc/superpowers/visualizer/css/base.css doc/superpowers/visualizer/index.html
git commit -m "feat(visualizer): add base layout and dark-lab theme"
```

---

### Task 3: Implement scene renderer and step player

**Files:**
- Modify: `doc/superpowers/visualizer/js/scene-renderer.js`
- Modify: `doc/superpowers/visualizer/js/step-player.js`
- Modify: `doc/superpowers/visualizer/css/nodes.css`
- Modify: `doc/superpowers/visualizer/css/animations.css`

- [ ] **Step 1: Implement scene-renderer.js**

In `doc/superpowers/visualizer/js/scene-renderer.js`:

```js
export class SceneRenderer {
  constructor(stageSvg, stageLabel, details, registerTable) {
    this.stageSvg = stageSvg;
    this.stageLabel = stageLabel;
    this.details = details;
    this.registerTable = registerTable;
    this.currentScene = null;
  }

  load(scene) {
    this.currentScene = scene;
    this.stageLabel.textContent = `${scene.title} · Step 0 / ${scene.steps.length}`;
    this.stageSvg.innerHTML = '';
    this.stageSvg.appendChild(scene.svg());
    this.details.title.textContent = scene.title;
    this.details.step.textContent = '';
    this.details.desc.textContent = '点击「播放」或「下一步」开始。';
    this.details.source.innerHTML = '';
    this.renderRegisters(scene.initialRegisters || {});
  }

  renderStep(step, index, total) {
    this.stageLabel.textContent = `${this.currentScene.title} · Step ${index + 1} / ${total}`;

    // Update nodes
    this.stageSvg.querySelectorAll('[data-node]').forEach(node => {
      const id = node.getAttribute('data-node');
      if (step.highlight.includes(id)) {
        node.classList.add('active');
        node.classList.remove('dim');
      } else if (step.dim?.includes(id)) {
        node.classList.add('dim');
        node.classList.remove('active');
      } else {
        node.classList.remove('active', 'dim');
      }
    });

    // Update arrows
    this.stageSvg.querySelectorAll('[data-arrow]').forEach(arrow => {
      const id = arrow.getAttribute('data-arrow');
      if (step.arrows.includes(id)) {
        arrow.classList.add('flow');
      } else {
        arrow.classList.remove('flow');
      }
    });

    // Update details
    this.details.step.textContent = step.label;
    this.details.title.textContent = step.title || this.currentScene.title;
    this.details.desc.textContent = step.detail;
    if (step.source) {
      this.details.source.innerHTML = `<span>Source: </span><a href="../../..${step.source.split(':')[0]}" target="_blank">${step.source}</a>`;
    } else {
      this.details.source.innerHTML = '';
    }

    // Update registers
    this.renderRegisters(step.registerChange || {});
  }

  renderRegisters(changes) {
    const base = this.currentScene?.initialRegisters || {};
    const registers = { ...base, ...changes };
    this.registerTable.innerHTML = Object.entries(registers)
      .map(([name, value]) => {
        const changed = name in changes;
        return `<div class="register-row ${changed ? 'changed' : ''}">
          <span class="register-name">${name}</span>
          <span class="register-value">${value}</span>
        </div>`;
      })
      .join('');
  }
}
```

- [ ] **Step 2: Implement step-player.js**

In `doc/superpowers/visualizer/js/step-player.js`:

```js
export class StepPlayer {
  constructor(renderer, onProgress) {
    this.renderer = renderer;
    this.onProgress = onProgress;
    this.scene = null;
    this.current = -1;
    this.playing = false;
    this.timer = null;
    this.speed = 1000;
  }

  setScene(scene) {
    this.scene = scene;
    this.current = -1;
    this.playing = false;
    this.stopTimer();
    this.renderer.load(scene);
    this.onProgress(0, scene.steps.length);
  }

  play() {
    if (this.playing) return;
    this.playing = true;
    if (this.current >= this.scene.steps.length - 1) {
      this.current = -1;
    }
    this.step();
  }

  pause() {
    this.playing = false;
    this.stopTimer();
  }

  next() {
    if (this.current < this.scene.steps.length - 1) {
      this.current++;
      this.render();
    }
  }

  prev() {
    if (this.current > 0) {
      this.current--;
      this.render();
    }
  }

  reset() {
    this.pause();
    this.current = -1;
    this.renderer.load(this.scene);
    this.onProgress(0, this.scene.steps.length);
  }

  step() {
    if (!this.playing) return;
    this.next();
    if (this.current < this.scene.steps.length - 1) {
      this.timer = setTimeout(() => this.step(), this.speed);
    } else {
      this.playing = false;
    }
  }

  setSpeed(ms) {
    this.speed = ms;
  }

  stopTimer() {
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = null;
    }
  }

  render() {
    const step = this.scene.steps[this.current];
    this.renderer.renderStep(step, this.current, this.scene.steps.length);
    this.onProgress(this.current + 1, this.scene.steps.length);
  }
}
```

- [ ] **Step 3: Write nodes.css**

In `doc/superpowers/visualizer/css/nodes.css`:

```css
[data-node] {
  fill: var(--bg-panel);
  stroke: var(--text-dim);
  stroke-width: 1.5;
  transition: all 0.3s ease;
}

[data-node] text {
  fill: var(--text-muted);
  font-family: var(--font-ui);
  font-size: 12px;
  transition: fill 0.3s ease;
}

[data-node].active {
  stroke: var(--accent);
  stroke-width: 2;
  fill: rgba(250, 250, 249, 0.08);
}

[data-node].active text {
  fill: var(--text-primary);
}

[data-node].dim {
  opacity: 0.4;
}

[data-arrow] {
  fill: none;
  stroke: var(--text-dim);
  stroke-width: 1.5;
  stroke-dasharray: 8 4;
  opacity: 0.4;
}

[data-arrow].flow {
  stroke: var(--accent);
  opacity: 1;
  animation: dash-flow 1s linear infinite;
}

@keyframes dash-flow {
  to {
    stroke-dashoffset: -24;
  }
}
```

- [ ] **Step 4: Write animations.css**

In `doc/superpowers/visualizer/css/animations.css`:

```css
.step-enter {
  animation: fade-in 0.25s ease;
}

@keyframes fade-in {
  from { opacity: 0; transform: translateY(4px); }
  to { opacity: 1; transform: translateY(0); }
}

.progress-fill {
  transition: width 0.25s ease;
}
```

- [ ] **Step 5: Verify player module loads without errors**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Expected: Console shows no module load errors.

- [ ] **Step 6: Commit**

```bash
git add doc/superpowers/visualizer/js/scene-renderer.js \
        doc/superpowers/visualizer/js/step-player.js \
        doc/superpowers/visualizer/css/nodes.css \
        doc/superpowers/visualizer/css/animations.css
git commit -m "feat(visualizer): add scene renderer and step player"
```

---

### Task 4: Wire app.js and navigation

**Files:**
- Modify: `doc/superpowers/visualizer/js/app.js`
- Modify: `doc/superpowers/visualizer/js/scenes/index.js`
- Modify: `doc/superpowers/visualizer/index.html` (if needed)

- [ ] **Step 1: Implement scenes/index.js**

In `doc/superpowers/visualizer/js/scenes/index.js`:

```js
import { bootScene } from './boot.js';
import { contextSwitchScene } from './context-switch.js';
import { schedulerScene } from './scheduler.js';
import { smpIpiScene } from './smp-ipi.js';
import { syncScene } from './sync.js';
import { vfsScene } from './vfs.js';
import { interruptScene } from './interrupt.js';
import { memoryScene } from './memory.js';
import { registersScene } from './registers.js';
import { shellScene } from './shell.js';

export const scenes = [
  bootScene,
  contextSwitchScene,
  schedulerScene,
  smpIpiScene,
  syncScene,
  vfsScene,
  interruptScene,
  memoryScene,
  registersScene,
  shellScene,
];
```

- [ ] **Step 2: Implement app.js**

In `doc/superpowers/visualizer/js/app.js`:

```js
import { SceneRenderer } from './scene-renderer.js';
import { StepPlayer } from './step-player.js';
import { scenes } from './scenes/index.js';

const moduleList = document.getElementById('module-list');
const stageSvg = document.getElementById('stage-svg');
const stageLabel = document.getElementById('stage-label');
const details = {
  step: document.getElementById('details-step'),
  title: document.getElementById('details-title'),
  desc: document.getElementById('details-desc'),
  source: document.getElementById('details-source'),
};
const registerTable = document.getElementById('register-table');
const progressFill = document.getElementById('progress-fill');
const btnPlay = document.getElementById('btn-play');
const btnPrev = document.getElementById('btn-prev');
const btnNext = document.getElementById('btn-next');
const btnReset = document.getElementById('btn-reset');
const speedSelect = document.getElementById('speed');

const renderer = new SceneRenderer(stageSvg, stageLabel, details, registerTable);
const player = new StepPlayer(renderer, (current, total) => {
  progressFill.style.width = `${(current / total) * 100}%`;
});

function renderModuleList() {
  moduleList.innerHTML = '';
  scenes.forEach((scene, index) => {
    const li = document.createElement('li');
    li.className = 'module-item';
    li.textContent = scene.title;
    li.dataset.index = index;
    li.addEventListener('click', () => selectModule(index));
    moduleList.appendChild(li);
  });
}

function selectModule(index) {
  document.querySelectorAll('.module-item').forEach((el, i) => {
    el.classList.toggle('active', i === index);
  });
  player.setScene(scenes[index]);
  updatePlayButton(false);
}

function updatePlayButton(playing) {
  btnPlay.textContent = playing ? '暂停' : '播放';
}

btnPlay.addEventListener('click', () => {
  if (player.playing) {
    player.pause();
    updatePlayButton(false);
  } else {
    player.play();
    updatePlayButton(true);
    // Stop updating button text when auto-play finishes
    const check = setInterval(() => {
      if (!player.playing) {
        updatePlayButton(false);
        clearInterval(check);
      }
    }, 100);
  }
});

btnNext.addEventListener('click', () => {
  player.pause();
  updatePlayButton(false);
  player.next();
});

btnPrev.addEventListener('click', () => {
  player.pause();
  updatePlayButton(false);
  player.prev();
});

btnReset.addEventListener('click', () => {
  player.reset();
  updatePlayButton(false);
});

speedSelect.addEventListener('change', (e) => {
  player.setSpeed(parseInt(e.target.value, 10));
});

document.addEventListener('keydown', (e) => {
  if (e.code === 'Space') {
    e.preventDefault();
    btnPlay.click();
  } else if (e.code === 'ArrowRight') {
    btnNext.click();
  } else if (e.code === 'ArrowLeft') {
    btnPrev.click();
  }
});

renderModuleList();
selectModule(0);
```

- [ ] **Step 3: Test navigation and controls**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Expected: First module auto-loads. Clicking module names switches. Next/Prev/Play/Reset buttons work. Space/arrow keys work.

- [ ] **Step 4: Commit**

```bash
git add doc/superpowers/visualizer/js/app.js \
        doc/superpowers/visualizer/js/scenes/index.js
git commit -m "feat(visualizer): wire app, navigation and keyboard shortcuts"
```

---

### Task 5: Implement context-switch scene (pilot)

**Files:**
- Modify: `doc/superpowers/visualizer/js/scenes/context-switch.js`

- [ ] **Step 1: Write SVG factory and scene data**

In `doc/superpowers/visualizer/js/scenes/context-switch.js`:

```js
function createSvg() {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 800 400');
  svg.innerHTML = `
    <defs>
      <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
        <polygon points="0 0, 10 3.5, 0 7" fill="#71717a" />
      </marker>
      <marker id="arrowhead-active" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
        <polygon points="0 0, 10 3.5, 0 7" fill="#fafaf9" />
      </marker>
    </defs>

    <g data-node="current-task">
      <rect x="80" y="80" width="120" height="60" rx="6" />
      <text x="140" y="115" text-anchor="middle">当前任务</text>
    </g>

    <g data-node="cpu">
      <rect x="340" y="60" width="120" height="100" rx="6" />
      <text x="400" y="115" text-anchor="middle">CPU 现场</text>
    </g>

    <g data-node="next-task">
      <rect x="600" y="80" width="120" height="60" rx="6" />
      <text x="660" y="115" text-anchor="middle">下一个任务</text>
    </g>

    <g data-node="tcb-a">
      <rect x="80" y="260" width="120" height="60" rx="6" />
      <text x="140" y="295" text-anchor="middle">TCB A</text>
    </g>

    <g data-node="tcb-b">
      <rect x="600" y="260" width="120" height="60" rx="6" />
      <text x="660" y="295" text-anchor="middle">TCB B</text>
    </g>

    <path data-arrow="save-to-tcb" d="M 400 160 L 400 220 L 200 260" marker-end="url(#arrowhead)" />
    <path data-arrow="pick-next" d="M 460 110 L 560 110" marker-end="url(#arrowhead)" />
    <path data-arrow="restore-from-tcb" d="M 660 260 L 660 200 L 460 160" marker-end="url(#arrowhead)" />
  `;
  return svg;
}

export const contextSwitchScene = {
  id: 'context-switch',
  title: '上下文切换',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
    A0: '0x00000000',
    A1: '0x3FFC7FF0',
  },
  svg: createSvg,
  steps: [
    {
      id: 'trigger',
      label: '触发切换',
      highlight: ['current-task', 'cpu'],
      arrows: [],
      detail: '当前任务因 tick、阻塞或主动让出而触发调度。CPU 即将保存其现场。',
      source: 'src/kernel/kern_sched.c:xxx',
    },
    {
      id: 'save-registers',
      label: '保存通用寄存器',
      highlight: ['cpu', 'tcb-a'],
      arrows: ['save-to-tcb'],
      detail: '调用 save_context，把 A0-A15、PC、PS 等寄存器压入当前任务的内核栈，并在 TCB 中记录栈指针。',
      source: 'src/kernel/esp32/kern_esp32_context.S:42-58',
      registerChange: { SP: '0x3FFC7F80' },
    },
    {
      id: 'pick-next',
      label: '选择下一个任务',
      highlight: ['cpu'],
      arrows: ['pick-next'],
      detail: '调度器遍历优先级桶，选择最高优先级就绪任务作为下一个运行任务。',
      source: 'src/kernel/kern_sched.c:xxx',
    },
    {
      id: 'switch-stack',
      label: '切换到新任务栈',
      highlight: ['cpu', 'tcb-b'],
      arrows: [],
      detail: '将 SP 指向 TCB B 中保存的栈顶地址，准备恢复其寄存器。',
      source: 'src/kernel/esp32/kern_esp32_context.S:72-80',
      registerChange: { SP: '0x3FFC9F40' },
    },
    {
      id: 'restore-registers',
      label: '恢复寄存器并返回',
      highlight: ['cpu', 'next-task'],
      arrows: ['restore-from-tcb'],
      detail: '从 TCB B 的栈中弹出寄存器，恢复 PC 和 PS，最后执行 ret 进入新任务。',
      source: 'src/kernel/esp32/kern_esp32_context.S:82-100',
      registerChange: { PC: '0x40082340', A0: '0x00000001' },
    },
  ],
};
```

- [ ] **Step 2: Verify scene plays correctly**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Expected: Context switch scene shows 5 steps, nodes/arrows animate, register panel updates.

- [ ] **Step 3: Commit**

```bash
git add doc/superpowers/visualizer/js/scenes/context-switch.js
git commit -m "feat(visualizer): add context-switch scene"
```

---

### Task 6: Implement boot scene

**Files:**
- Modify: `doc/superpowers/visualizer/js/scenes/boot.js`

- [ ] **Step 1: Write SVG and scene data**

In `doc/superpowers/visualizer/js/scenes/boot.js`:

```js
function createSvg() {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 800 400');
  svg.innerHTML = `
    <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#71717a" />
    </marker>
    <marker id="arrowhead-active" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#fafaf9" />
    </marker>

    <g data-node="reset">
      <rect x="40" y="160" width="100" height="50" rx="6" />
      <text x="90" y="190" text-anchor="middle">复位向量</text>
    </g>

    <g data-node="hw-init">
      <rect x="200" y="160" width="120" height="50" rx="6" />
      <text x="260" y="190" text-anchor="middle">硬件初始化</text>
    </g>

    <g data-node="kernel-init">
      <rect x="380" y="160" width="120" height="50" rx="6" />
      <text x="440" y="190" text-anchor="middle">内核子系统初始化</text>
    </g>

    <g data-node="create-init">
      <rect x="560" y="160" width="120" height="50" rx="6" />
      <text x="620" y="190" text-anchor="middle">创建 init 任务</text>
    </g>

    <g data-node="schedule">
      <rect x="720" y="160" width="80" height="50" rx="6" />
      <text x="760" y="190" text-anchor="middle">调度</text>
    </g>

    <path data-arrow="reset-to-hw" d="M 140 185 L 190 185" marker-end="url(#arrowhead)" />
    <path data-arrow="hw-to-kernel" d="M 320 185 L 370 185" marker-end="url(#arrowhead)" />
    <path data-arrow="kernel-to-create" d="M 500 185 L 550 185" marker-end="url(#arrowhead)" />
    <path data-arrow="create-to-schedule" d="M 680 185 L 710 185" marker-end="url(#arrowhead)" />
  `;
  return svg;
}

export const bootScene = {
  id: 'boot',
  title: '系统启动流程',
  initialRegisters: {
    PC: '0x40000400',
    SP: '0x3FFC_0000',
    PS: '0x0006_001E',
    A0: '0x0000_0000',
  },
  svg: createSvg,
  steps: [
    {
      id: 'reset-vector',
      label: '从复位向量启动',
      highlight: ['reset'],
      arrows: [],
      detail: 'ESP32 上电后从 ROM 复位向量开始执行，初始化堆栈并跳转到入口代码。',
      source: 'src/kernel/esp32/kern_esp32_start.S:xxx',
    },
    {
      id: 'hw-init',
      label: '初始化硬件',
      highlight: ['hw-init'],
      arrows: ['reset-to-hw'],
      detail: '配置时钟、UART、中断控制器、看门狗和 GPIO，为内核运行准备硬件环境。',
      source: 'src/hal/hal_dreamCore/...:xxx',
    },
    {
      id: 'kernel-init',
      label: '初始化内核子系统',
      highlight: ['kernel-init'],
      arrows: ['hw-to-kernel'],
      detail: '依次初始化调度器、内存分配器、VFS、定时器、同步原语和 SMP。',
      source: 'src/kernel/kern_init.c:xxx',
    },
    {
      id: 'create-init-task',
      label: '创建第一个任务',
      highlight: ['create-init'],
      arrows: ['kernel-to-create'],
      detail: '创建 init 任务，作为用户空间的第一个入口。',
      source: 'src/kernel/kern_task.c:xxx',
    },
    {
      id: 'start-scheduler',
      label: '启动调度器',
      highlight: ['schedule'],
      arrows: ['create-to-schedule'],
      detail: '打开 tick 定时器，触发第一次上下文切换，开始多任务调度。',
      source: 'src/kernel/kern_sched.c:xxx',
    },
  ],
};
```

- [ ] **Step 2: Verify boot scene plays**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Expected: Boot scene shows 5 steps in sequence.

- [ ] **Step 3: Commit**

```bash
git add doc/superpowers/visualizer/js/scenes/boot.js
git commit -m "feat(visualizer): add boot scene"
```

---

### Task 7: Implement scheduler scene

**Files:**
- Modify: `doc/superpowers/visualizer/js/scenes/scheduler.js`

- [ ] **Step 1: Write scheduler scene**

In `doc/superpowers/visualizer/js/scenes/scheduler.js`:

```js
function createSvg() {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 800 400');
  svg.innerHTML = `
    <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#71717a" />
    </marker>

    <g data-node="tick">
      <rect x="40" y="160" width="100" height="50" rx="6" />
      <text x="90" y="190" text-anchor="middle">Tick 中断</text>
    </g>

    <g data-node="timeslice">
      <rect x="200" y="160" width="120" height="50" rx="6" />
      <text x="260" y="190" text-anchor="middle">更新时间片</text>
    </g>

    <g data-node="buckets">
      <rect x="380" y="120" width="160" height="130" rx="6" />
      <text x="460" y="185" text-anchor="middle">优先级桶</text>
    </g>

    <g data-node="select">
      <rect x="600" y="160" width="140" height="50" rx="6" />
      <text x="670" y="190" text-anchor="middle">选择最高优先级</text>
    </g>

    <path data-arrow="tick-to-slice" d="M 140 185 L 190 185" marker-end="url(#arrowhead)" />
    <path data-arrow="slice-to-buckets" d="M 320 185 L 370 185" marker-end="url(#arrowhead)" />
    <path data-arrow="buckets-to-select" d="M 540 185 L 590 185" marker-end="url(#arrowhead)" />
  `;
  return svg;
}

export const schedulerScene = {
  id: 'scheduler',
  title: '调度器流程',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createSvg,
  steps: [
    {
      id: 'tick-entry',
      label: 'Tick 中断入口',
      highlight: ['tick'],
      arrows: [],
      detail: '定时器产生周期中断，进入 tick handler。',
      source: 'src/kernel/kern_timer.c:xxx',
    },
    {
      id: 'update-timeslice',
      label: '递减当前任务时间片',
      highlight: ['timeslice'],
      arrows: ['tick-to-slice'],
      detail: '如果当前任务时间片耗尽，标记需要重新调度。',
      source: 'src/kernel/kern_sched.c:xxx',
    },
    {
      id: 'scan-buckets',
      label: '扫描优先级桶',
      highlight: ['buckets'],
      arrows: ['slice-to-buckets'],
      detail: '从最高优先级开始遍历就绪队列桶，找到非空桶。',
      source: 'src/kernel/kern_sched.c:xxx',
    },
    {
      id: 'select-task',
      label: '选择下一个任务',
      highlight: ['select'],
      arrows: ['buckets-to-select'],
      detail: '从最高优先级桶中取出队首任务作为下一个运行任务。',
      source: 'src/kernel/kern_sched.c:xxx',
    },
  ],
};
```

- [ ] **Step 2: Verify and commit**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Expected: Scheduler scene plays 4 steps.

```bash
git add doc/superpowers/visualizer/js/scenes/scheduler.js
git commit -m "feat(visualizer): add scheduler scene"
```

---

### Task 8: Implement SMP/IPI scene

**Files:**
- Modify: `doc/superpowers/visualizer/js/scenes/smp-ipi.js`

- [ ] **Step 1: Write SMP/IPI scene**

In `doc/superpowers/visualizer/js/scenes/smp-ipi.js`:

```js
function createSvg() {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 800 400');
  svg.innerHTML = `
    <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#71717a" />
    </marker>

    <g data-node="cpu0">
      <rect x="80" y="80" width="140" height="80" rx="6" />
      <text x="150" y="125" text-anchor="middle">CPU0 调度器</text>
    </g>

    <g data-node="cpu1">
      <rect x="80" y="240" width="140" height="80" rx="6" />
      <text x="150" y="285" text-anchor="middle">CPU1 调度器</text>
    </g>

    <g data-node="ipi">
      <rect x="360" y="160" width="100" height="80" rx="6" />
      <text x="410" y="205" text-anchor="middle">IPI</text>
    </g>

    <g data-node="migrate">
      <rect x="560" y="160" width="140" height="80" rx="6" />
      <text x="630" y="205" text-anchor="middle">任务迁移/唤醒</text>
    </g>

    <path data-arrow="cpu0-to-ipi" d="M 220 120 L 360 180" marker-end="url(#arrowhead)" />
    <path data-arrow="cpu1-to-ipi" d="M 220 280 L 360 220" marker-end="url(#arrowhead)" />
    <path data-arrow="ipi-to-migrate" d="M 460 200 L 560 200" marker-end="url(#arrowhead)" />
  `;
  return svg;
}

export const smpIpiScene = {
  id: 'smp-ipi',
  title: 'SMP 双核调度与 IPI',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createSvg,
  steps: [
    {
      id: 'per-cpu-sched',
      label: '每个 CPU 独立调度',
      highlight: ['cpu0', 'cpu1'],
      arrows: [],
      detail: 'CPU0 和 CPU1 各自维护自己的就绪队列和当前任务。',
      source: 'src/kernel/kern_smp.c:xxx',
    },
    {
      id: 'cpu0-ipi',
      label: 'CPU0 发送 IPI',
      highlight: ['cpu0', 'ipi'],
      arrows: ['cpu0-to-ipi'],
      detail: '当 CPU0 唤醒一个绑定在 CPU1 上的任务时，触发跨核中断通知 CPU1。',
      source: 'src/kernel/kern_smp.c:xxx',
    },
    {
      id: 'cpu1-receive',
      label: 'CPU1 接收 IPI',
      highlight: ['cpu1', 'ipi'],
      arrows: ['cpu1-to-ipi'],
      detail: 'CPU1 进入 IPI handler，检查是否有高优先级任务需要立即调度。',
      source: 'src/kernel/esp32/kern_esp32_ipi.S:xxx',
    },
    {
      id: 'migrate-wake',
      label: '任务迁移或唤醒',
      highlight: ['migrate'],
      arrows: ['ipi-to-migrate'],
      detail: 'CPU1 将被唤醒的任务加入自己的就绪队列，必要时触发上下文切换。',
      source: 'src/kernel/kern_smp.c:xxx',
    },
  ],
};
```

- [ ] **Step 2: Verify and commit**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Expected: SMP/IPI scene plays 4 steps.

```bash
git add doc/superpowers/visualizer/js/scenes/smp-ipi.js
git commit -m "feat(visualizer): add smp-ipi scene"
```

---

### Task 9: Implement sync primitives scene

**Files:**
- Modify: `doc/superpowers/visualizer/js/scenes/sync.js`

- [ ] **Step 1: Write sync scene**

In `doc/superpowers/visualizer/js/scenes/sync.js`:

```js
function createSvg() {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 800 400');
  svg.innerHTML = `
    <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#71717a" />
    </marker>

    <g data-node="task-a">
      <rect x="60" y="80" width="120" height="60" rx="6" />
      <text x="120" y="115" text-anchor="middle">任务 A</text>
    </g>

    <g data-node="lock">
      <rect x="340" y="80" width="120" height="60" rx="6" />
      <text x="400" y="115" text-anchor="middle">同步对象</text>
    </g>

    <g data-node="task-b">
      <rect x="620" y="80" width="120" height="60" rx="6" />
      <text x="680" y="115" text-anchor="middle">任务 B</text>
    </g>

    <g data-node="wait-queue">
      <rect x="340" y="260" width="120" height="60" rx="6" />
      <text x="400" y="295" text-anchor="middle">等待队列</text>
    </g>

    <path data-arrow="a-to-lock" d="M 180 110 L 340 110" marker-end="url(#arrowhead)" />
    <path data-arrow="b-to-lock" d="M 620 110 L 460 110" marker-end="url(#arrowhead)" />
    <path data-arrow="lock-to-wait" d="M 400 140 L 400 260" marker-end="url(#arrowhead)" />
    <path data-arrow="wait-to-lock" d="M 380 260 L 380 140" marker-end="url(#arrowhead)" />
  `;
  return svg;
}

export const syncScene = {
  id: 'sync',
  title: '同步原语流程',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createSvg,
  steps: [
    {
      id: 'task-a-lock',
      label: '任务 A 尝试获取锁',
      highlight: ['task-a', 'lock'],
      arrows: ['a-to-lock'],
      detail: '任务 A 调用 mutex_lock / spinlock_lock，尝试进入临界区。',
      source: 'src/kernel/kern_sync.c:xxx',
    },
    {
      id: 'task-b-block',
      label: '任务 B 阻塞等待',
      highlight: ['task-b', 'wait-queue'],
      arrows: ['b-to-lock', 'lock-to-wait'],
      detail: '锁已被占用，任务 B 被放入等待队列并触发调度。',
      source: 'src/kernel/kern_sync.c:xxx',
    },
    {
      id: 'task-a-unlock',
      label: '任务 A 释放锁',
      highlight: ['task-a', 'lock'],
      arrows: [],
      detail: '任务 A 离开临界区，释放锁，并检查等待队列。',
      source: 'src/kernel/kern_sync.c:xxx',
    },
    {
      id: 'wake-b',
      label: '唤醒任务 B',
      highlight: ['wait-queue', 'task-b'],
      arrows: ['wait-to-lock'],
      detail: '从等待队列取出任务 B，标记为就绪状态。若 B 优先级更高，触发优先级继承或调度。',
      source: 'src/kernel/kern_sync.c:xxx',
    },
  ],
};
```

- [ ] **Step 2: Verify and commit**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Expected: Sync scene plays 4 steps.

```bash
git add doc/superpowers/visualizer/js/scenes/sync.js
git commit -m "feat(visualizer): add sync primitives scene"
```

---

### Task 10: Implement VFS scene

**Files:**
- Modify: `doc/superpowers/visualizer/js/scenes/vfs.js`

- [ ] **Step 1: Write VFS scene**

In `doc/superpowers/visualizer/js/scenes/vfs.js`:

```js
function createSvg() {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 800 400');
  svg.innerHTML = `
    <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#71717a" />
    </marker>

    <g data-node="app">
      <rect x="40" y="160" width="100" height="50" rx="6" />
      <text x="90" y="190" text-anchor="middle">应用</text>
    </g>

    <g data-node="vfs">
      <rect x="200" y="160" width="100" height="50" rx="6" />
      <text x="250" y="190" text-anchor="middle">VFS 层</text>
    </g>

    <g data-node="inode">
      <rect x="360" y="160" width="100" height="50" rx="6" />
      <text x="410" y="190" text-anchor="middle">inode 查找</text>
    </g>

    <g data-node="dev">
      <rect x="520" y="160" width="100" height="50" rx="6" />
      <text x="570" y="190" text-anchor="middle">设备文件</text>
    </g>

    <g data-node="driver">
      <rect x="680" y="160" width="100" height="50" rx="6" />
      <text x="730" y="190" text-anchor="middle">驱动回调</text>
    </g>

    <path data-arrow="app-to-vfs" d="M 140 185 L 190 185" marker-end="url(#arrowhead)" />
    <path data-arrow="vfs-to-inode" d="M 300 185 L 350 185" marker-end="url(#arrowhead)" />
    <path data-arrow="inode-to-dev" d="M 460 185 L 510 185" marker-end="url(#arrowhead)" />
    <path data-arrow="dev-to-driver" d="M 620 185 L 670 185" marker-end="url(#arrowhead)" />
  `;
  return svg;
}

export const vfsScene = {
  id: 'vfs',
  title: 'VFS 调用流程',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createSvg,
  steps: [
    {
      id: 'sys-call',
      label: '应用发起系统调用',
      highlight: ['app'],
      arrows: [],
      detail: '应用调用 open/read/write/close 等文件操作。',
      source: 'src/kernel/kern_vfs.c:xxx',
    },
    {
      id: 'vfs-dispatch',
      label: 'VFS 统一入口',
      highlight: ['vfs'],
      arrows: ['app-to-vfs'],
      detail: 'VFS 层根据文件路径解析文件描述符，进入 inode 查找流程。',
      source: 'src/kernel/kern_vfs.c:xxx',
    },
    {
      id: 'inode-lookup',
      label: 'inode 查找',
      highlight: ['inode'],
      arrows: ['vfs-to-inode'],
      detail: '根据路径逐层查找 dentry 和 inode，确定文件类型（普通/设备/特殊）。',
      source: 'src/kernel/kern_vfs.c:xxx',
    },
    {
      id: 'device-file',
      label: '设备文件分发',
      highlight: ['dev'],
      arrows: ['inode-to-dev'],
      detail: '如果是设备文件（如 /dev/ttyS0），根据主次设备号找到对应驱动。',
      source: 'src/kernel/kern_vfs.c:xxx',
    },
    {
      id: 'driver-callback',
      label: '驱动回调执行',
      highlight: ['driver'],
      arrows: ['dev-to-driver'],
      detail: '调用驱动注册的 read/write 回调，完成实际硬件操作或数据返回。',
      source: 'src/kernel/devices/...:xxx',
    },
  ],
};
```

- [ ] **Step 2: Verify and commit**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Expected: VFS scene plays 5 steps.

```bash
git add doc/superpowers/visualizer/js/scenes/vfs.js
git commit -m "feat(visualizer): add vfs scene"
```

---

### Task 11: Implement interrupt scene

**Files:**
- Modify: `doc/superpowers/visualizer/js/scenes/interrupt.js`

- [ ] **Step 1: Write interrupt scene**

In `doc/superpowers/visualizer/js/scenes/interrupt.js`:

```js
function createSvg() {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 800 400');
  svg.innerHTML = `
    <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#71717a" />
    </marker>

    <g data-node="irq">
      <rect x="40" y="160" width="100" height="50" rx="6" />
      <text x="90" y="190" text-anchor="middle">中断信号</text>
    </g>

    <g data-node="entry">
      <rect x="200" y="160" width="120" height="50" rx="6" />
      <text x="260" y="190" text-anchor="middle">保存上下文</text>
    </g>

    <g data-node="dispatch">
      <rect x="380" y="160" width="120" height="50" rx="6" />
      <text x="440" y="190" text-anchor="middle">分发 ISR</text>
    </g>

    <g data-node="sched-check">
      <rect x="560" y="160" width="120" height="50" rx="6" />
      <text x="620" y="190" text-anchor="middle">调度检查</text>
    </g>

    <g data-node="return"
003e
      <rect x="720" y="160" width="80" height="50" rx="6" />
      <text x="760" y="190" text-anchor="middle">返回</text>
    </g>

    <path data-arrow="irq-to-entry" d="M 140 185 L 190 185" marker-end="url(#arrowhead)" />
    <path data-arrow="entry-to-dispatch" d="M 320 185 L 370 185" marker-end="url(#arrowhead)" />
    <path data-arrow="dispatch-to-sched" d="M 500 185 L 550 185" marker-end="url(#arrowhead)" />
    <path data-arrow="sched-to-return" d="M 680 185 L 710 185" marker-end="url(#arrowhead)" />
  `;
  return svg;
}

export const interruptScene = {
  id: 'interrupt',
  title: '中断与异常处理',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createSvg,
  steps: [
    {
      id: 'irq-signal',
      label: '中断信号到达',
      highlight: ['irq'],
      arrows: [],
      detail: '外部设备或定时器触发中断，CPU 暂停当前指令流。',
      source: 'src/kernel/esp32/kern_esp32_int.S:xxx',
    },
    {
      id: 'save-context',
      label: '中断入口保存上下文',
      highlight: ['entry'],
      arrows: ['irq-to-entry'],
      detail: '进入汇编中断入口，保存 PC、PS、A0-A15 等到中断栈。',
      source: 'src/kernel/esp32/kern_esp32_int.S:xxx',
      registerChange: { SP: '0x3FFC7F00', PS: '0x0006001F' },
    },
    {
      id: 'dispatch-isr',
      label: '分发 ISR',
      highlight: ['dispatch'],
      arrows: ['entry-to-dispatch'],
      detail: '根据中断号查找中断描述符表，调用注册的 C 语言 ISR。',
      source: 'src/kernel/kern_int.c:xxx',
    },
    {
      id: 'sched-check',
      label: '调度检查',
      highlight: ['sched-check'],
      arrows: ['dispatch-to-sched'],
      detail: 'ISR 返回前检查是否需要重新调度（如 tick handler 减时间片）。',
      source: 'src/kernel/kern_sched.c:xxx',
    },
    {
      id: 'restore-return',
      label: '恢复上下文并返回',
      highlight: ['return'],
      arrows: ['sched-to-return'],
      detail: '恢复寄存器，执行 rfi 或 retw 返回被中断的任务或新任务。',
      source: 'src/kernel/esp32/kern_esp32_int.S:xxx',
      registerChange: { SP: '0x3FFC8000', PS: '0x0006001E' },
    },
  ],
};
```

- [ ] **Step 2: Verify and commit**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Expected: Interrupt scene plays 5 steps.

```bash
git add doc/superpowers/visualizer/js/scenes/interrupt.js
git commit -m "feat(visualizer): add interrupt handling scene"
```

---

### Task 12: Implement memory allocation scene

**Files:**
- Modify: `doc/superpowers/visualizer/js/scenes/memory.js`

- [ ] **Step 1: Write memory scene**

In `doc/superpowers/visualizer/js/scenes/memory.js`:

```js
function createSvg() {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 800 400');
  svg.innerHTML = `
    <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#71717a" />
    </marker>

    <g data-node="kmalloc">
      <rect x="40" y="160" width="100" height="50" rx="6" />
      <text x="90" y="190" text-anchor="middle">kmalloc</text>
    </g>

    <g data-node="free-list">
      <rect x="200" y="160" width="120" height="50" rx="6" />
      <text x="260" y="190" text-anchor="middle">空闲链表</text>
    </g>

    <g data-node="split">
      <rect x="380" y="160" width="120" height="50" rx="6" />
      <text x="440" y="190" text-anchor="middle">分割块</text>
    </g>

    <g data-node="allocated">
      <rect x="560" y="160" width="120" height="50" rx="6" />
      <text x="620" y="190" text-anchor="middle">返回指针</text>
    </g>

    <g data-node="kfree">
      <rect x="720" y="160" width="80" height="50" rx="6" />
      <text x="760" y="190" text-anchor="middle">kfree</text>
    </g>

    <path data-arrow="kmalloc-to-free" d="M 140 185 L 190 185" marker-end="url(#arrowhead)" />
    <path data-arrow="free-to-split" d="M 320 185 L 370 185" marker-end="url(#arrowhead)" />
    <path data-arrow="split-to-alloc" d="M 500 185 L 550 185" marker-end="url(#arrowhead)" />
    <path data-arrow="alloc-to-kfree" d="M 680 185 L 710 185" marker-end="url(#arrowhead)" />
  `;
  return svg;
}

export const memoryScene = {
  id: 'memory',
  title: '内存分配流程',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createSvg,
  steps: [
    {
      id: 'kmalloc-call',
      label: '调用 kmalloc',
      highlight: ['kmalloc'],
      arrows: [],
      detail: '内核代码请求分配指定大小的内存块。',
      source: 'src/kernel/kern_kmalloc.c:xxx',
    },
    {
      id: 'scan-free-list',
      label: '遍历空闲链表',
      highlight: ['free-list'],
      arrows: ['kmalloc-to-free'],
      detail: '从空闲链表头开始查找第一个足够大的空闲块。',
      source: 'src/kernel/kern_kmalloc.c:xxx',
    },
    {
      id: 'split-block',
      label: '分割空闲块',
      highlight: ['split'],
      arrows: ['free-to-split'],
      detail: '如果空闲块远大于请求大小，将其分割为已分配块和剩余空闲块。',
      source: 'src/kernel/kern_kmalloc.c:xxx',
    },
    {
      id: 'return-pointer',
      label: '返回内存指针',
      highlight: ['allocated'],
      arrows: ['split-to-alloc'],
      detail: '返回指向已分配内存的指针，并更新空闲链表。',
      source: 'src/kernel/kern_kmalloc.c:xxx',
    },
    {
      id: 'kfree-merge',
      label: 'kfree 释放与合并',
      highlight: ['kfree'],
      arrows: ['alloc-to-kfree'],
      detail: '释放内存时将其插回空闲链表，并尝试与相邻空闲块合并。',
      source: 'src/kernel/kern_kmalloc.c:xxx',
    },
  ],
};
```

- [ ] **Step 2: Verify and commit**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Expected: Memory scene plays 5 steps.

```bash
git add doc/superpowers/visualizer/js/scenes/memory.js
git commit -m "feat(visualizer): add memory allocation scene"
```

---

### Task 13: Implement register configuration scene

**Files:**
- Modify: `doc/superpowers/visualizer/js/scenes/registers.js`

- [ ] **Step 1: Write registers scene**

In `doc/superpowers/visualizer/js/scenes/registers.js`:

```js
function createSvg() {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 800 400');
  svg.innerHTML = `
    <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#71717a" />
    </marker>

    <g data-node="cp0">
      <rect x="40" y="160" width="120" height="50" rx="6" />
      <text x="100" y="190" text-anchor="middle">CPENABLE / CP0</text>
    </g>

    <g data-node="wdt">
      <rect x="220" y="160" width="120" height="50" rx="6" />
      <text x="280" y="190" text-anchor="middle">看门狗</text>
    </g>

    <g data-node="timer">
      <rect x="400" y="160" width="120" height="50" rx="6" />
      <text x="460" y="190" text-anchor="middle">定时器</text>
    </g>

    <g data-node="gpio">
      <rect x="580" y="160" width="120" height="50" rx="6" />
      <text x="640" y="190" text-anchor="middle">GPIO 矩阵</text>
    </g>

    <g data-node="int">
      <rect x="740" y="160" width="80" height="50" rx="6" />
      <text x="780" y="190" text-anchor="middle">中断</text>
    </g>

    <path data-arrow="cp0-to-wdt" d="M 160 185 L 210 185" marker-end="url(#arrowhead)" />
    <path data-arrow="wdt-to-timer" d="M 340 185 L 390 185" marker-end="url(#arrowhead)" />
    <path data-arrow="timer-to-gpio" d="M 520 185 L 570 185" marker-end="url(#arrowhead)" />
    <path data-arrow="gpio-to-int" d="M 700 185 L 730 185" marker-end="url(#arrowhead)" />
  `;
  return svg;
}

export const registersScene = {
  id: 'registers',
  title: '寄存器配置流程',
  initialRegisters: {
    PC: '0x40000400',
    SP: '0x3FFC0000',
    PS: '0x0006001E',
    'CPENABLE': '0x00000000',
  },
  svg: createSvg,
  steps: [
    {
      id: 'cpenable',
      label: '配置协处理器使能',
      highlight: ['cp0'],
      arrows: [],
      detail: '设置 CPENABLE 位，启用 FPU 等协处理器访问权限。',
      source: 'src/kernel/esp32/kern_esp32_start.S:xxx',
      registerChange: { CPENABLE: '0x00000001' },
    },
    {
      id: 'watchdog',
      label: '配置看门狗',
      highlight: ['wdt'],
      arrows: ['cp0-to-wdt'],
      detail: '初始化看门狗定时器，设置超时周期与喂狗机制。',
      source: 'src/kernel/kern_debug.c:xxx',
    },
    {
      id: 'timer',
      label: '配置系统定时器',
      highlight: ['timer'],
      arrows: ['wdt-to-timer'],
      detail: '配置 Xtensa 定时器产生周期性 tick 中断。',
      source: 'src/kernel/kern_timer.c:xxx',
    },
    {
      id: 'gpio-matrix',
      label: '配置 GPIO 矩阵',
      highlight: ['gpio'],
      arrows: ['timer-to-gpio'],
      detail: '将外设信号映射到物理 GPIO 引脚，如 SPI、UART。',
      source: 'src/hal/hal_dreamCore/...:xxx',
    },
    {
      id: 'interrupt',
      label: '使能中断控制器',
      highlight: ['int'],
      arrows: ['gpio-to-int'],
      detail: '配置中断优先级与中断向量表，最终打开全局中断。',
      source: 'src/kernel/kern_int.c:xxx',
    },
  ],
};
```

- [ ] **Step 2: Verify and commit**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Expected: Registers scene plays 5 steps.

```bash
git add doc/superpowers/visualizer/js/scenes/registers.js
git commit -m "feat(visualizer): add register configuration scene"
```

---

### Task 14: Implement shell execution scene

**Files:**
- Modify: `doc/superpowers/visualizer/js/scenes/shell.js`

- [ ] **Step 1: Write shell scene**

In `doc/superpowers/visualizer/js/scenes/shell.js`:

```js
function createSvg() {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 800 400');
  svg.innerHTML = `
    <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#71717a" />
    </marker>

    <g data-node="uart">
      <rect x="40" y="160" width="100" height="50" rx="6" />
      <text x="90" y="190" text-anchor="middle">串口输入</text>
    </g>

    <g data-node="buffer">
      <rect x="200" y="160" width="120" height="50" rx="6" />
      <text x="260" y="190" text-anchor="middle">命令缓冲区</text>
    </g>

    <g data-node="parse">
      <rect x="380" y="160" width="120" height="50" rx="6" />
      <text x="440" y="190" text-anchor="middle">解析参数</text>
    </g>

    <g data-node="lookup">
      <rect x="560" y="160" width="120" height="50" rx="6" />
      <text x="620" y="190" text-anchor="middle">查找命令表</text>
    </g>

    <g data-node="execute">
      <rect x="720" y="160" width="80" height="50" rx="6" />
      <text x="760" y="190" text-anchor="middle">执行</text>
    </g>

    <path data-arrow="uart-to-buffer" d="M 140 185 L 190 185" marker-end="url(#arrowhead)" />
    <path data-arrow="buffer-to-parse" d="M 320 185 L 370 185" marker-end="url(#arrowhead)" />
    <path data-arrow="parse-to-lookup" d="M 500 185 L 550 185" marker-end="url(#arrowhead)" />
    <path data-arrow="lookup-to-execute" d="M 680 185 L 710 185" marker-end="url(#arrowhead)" />
  `;
  return svg;
}

export const shellScene = {
  id: 'shell',
  title: 'Shell 执行流程',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createSvg,
  steps: [
    {
      id: 'uart-input',
      label: '串口接收字符',
      highlight: ['uart'],
      arrows: [],
      detail: 'Shell 任务从串口逐字符读取用户输入，直到收到换行。',
      source: 'src/kernel/kern_shell.c:xxx',
    },
    {
      id: 'line-buffer',
      label: '填充命令缓冲区',
      highlight: ['buffer'],
      arrows: ['uart-to-buffer'],
      detail: '将完整命令行存入缓冲区，等待解析。',
      source: 'src/kernel/kern_shell.c:xxx',
    },
    {
      id: 'parse-args',
      label: '解析命令与参数',
      highlight: ['parse'],
      arrows: ['buffer-to-parse'],
      detail: '按空格分割命令名和参数，处理引号与转义。',
      source: 'src/kernel/kern_shell.c:xxx',
    },
    {
      id: 'lookup-command',
      label: '查找命令表',
      highlight: ['lookup'],
      arrows: ['parse-to-lookup'],
      detail: '遍历内置命令表，匹配命令名并获取回调函数指针。',
      source: 'src/kernel/kern_shell.c:xxx',
    },
    {
      id: 'execute-output',
      label: '执行并输出',
      highlight: ['execute'],
      arrows: ['lookup-to-execute'],
      detail: '调用命令回调，将结果写回串口缓冲区输出。',
      source: 'src/kernel/kern_shell.c:xxx',
    },
  ],
};
```

- [ ] **Step 2: Verify and commit**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Expected: Shell scene plays 5 steps. All 10 modules navigable.

```bash
git add doc/superpowers/visualizer/js/scenes/shell.js
git commit -m "feat(visualizer): add shell execution scene"
```

---

### Task 15: Replace placeholder source line numbers

**Files:**
- Modify: all `doc/superpowers/visualizer/js/scenes/*.js` files

- [ ] **Step 1: Use codegraph to find real function locations**

Run for each key function:
```bash
# Example searches; repeat for each function referenced in scenes
codegraph_search "save_context"
codegraph_search "kern_sched_run"
codegraph_search "xTaskCreate"
codegraph_search "kern_vfs_open"
codegraph_search "kern_shell_exec"
```

Expected: codegraph returns file paths and line numbers.

- [ ] **Step 2: Update source fields in each scene file**

Replace all `:xxx` placeholders with real `path:line` references. For example, in `context-switch.js`:

```js
source: 'src/kernel/esp32/kern_esp32_context.S:42-58',
```

becomes the actual line range returned by codegraph.

- [ ] **Step 3: Verify links are plausible**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Click a few source links; they should open valid project files (or 404 if line anchors unsupported, which is acceptable).

- [ ] **Step 4: Commit**

```bash
git add doc/superpowers/visualizer/js/scenes/*.js
git commit -m "docs(visualizer): replace source placeholders with real line numbers"
```

---

### Task 16: Polish UI states and empty scene handling

**Files:**
- Modify: `doc/superpowers/visualizer/css/nodes.css`
- Modify: `doc/superpowers/visualizer/css/animations.css`
- Modify: `doc/superpowers/visualizer/js/scene-renderer.js`
- Modify: `doc/superpowers/visualizer/js/app.js`

- [ ] **Step 1: Add "completed" state for past nodes**

Append to `doc/superpowers/visualizer/css/nodes.css`:

```css
[data-node].done {
  stroke: #52525b;
  fill: rgba(250, 250, 249, 0.04);
}

[data-node].done text {
  fill: var(--text-dim);
}
```

- [ ] **Step 2: Track done state in renderer**

Modify `renderStep` in `scene-renderer.js` to mark previously highlighted nodes as `done`:

```js
// Inside renderStep, before setting active:
this.stageSvg.querySelectorAll('[data-node]').forEach(node => {
  if (node.classList.contains('active')) {
    node.classList.remove('active');
    node.classList.add('done');
  }
});

// Then apply current step classes as before
```

- [ ] **Step 3: Add subtle step-enter animation to details panel**

In `scene-renderer.js`, add a CSS class to details container on each step:

```js
this.details.desc.parentElement.classList.remove('step-enter');
void this.details.desc.parentElement.offsetWidth; // reflow
this.details.desc.parentElement.classList.add('step-enter');
```

- [ ] **Step 4: Verify polish**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Expected: Past nodes show dimmed "done" state; details panel animates on step change.

- [ ] **Step 5: Commit**

```bash
git add doc/superpowers/visualizer/css/nodes.css \
        doc/superpowers/visualizer/css/animations.css \
        doc/superpowers/visualizer/js/scene-renderer.js \
        doc/superpowers/visualizer/js/app.js
git commit -m "feat(visualizer): add done-state and step-enter animation"
```

---

### Task 17: Update documentation index

**Files:**
- Modify: `doc/index.md`

- [ ] **Step 1: Add visualizer entry to knowledge map**

Locate the section in `doc/index.md` that lists learning/development tools. Add:

```markdown
## 可视化工具

- **[Xeros Kernel Visualizer](superpowers/visualizer/index.html)** — 内核流程逐步动画演示
```

If no such section exists, append it near the end of the file.

- [ ] **Step 2: Verify link path is correct**

The relative path from `doc/index.md` to the visualizer is `superpowers/visualizer/index.html`.

- [ ] **Step 3: Commit**

```bash
git add doc/index.md
git commit -m "docs(visualizer): add visualizer entry to knowledge map"
```

---

### Task 18: Final verification

**Files:**
- All files in `doc/superpowers/visualizer/`
- `doc/index.md`

- [ ] **Step 1: Open visualizer and run through all modules**

Run:
```bash
open doc/superpowers/visualizer/index.html
```

Manually verify:
- [ ] 10 modules appear in left navigation
- [ ] Each module plays through all steps
- [ ] Nodes and arrows highlight correctly
- [ ] Register panel updates on relevant steps
- [ ] Play/pause/reset buttons work
- [ ] Next/previous buttons work
- [ ] Keyboard shortcuts work
- [ ] No console errors

- [ ] **Step 2: Check doc/index.md link**

Open `doc/index.md` and confirm the visualizer link points to `superpowers/visualizer/index.html`.

- [ ] **Step 3: Run git diff review**

```bash
git diff --stat
```

Expected: Only intended files modified under `doc/superpowers/visualizer/` and `doc/index.md`.

- [ ] **Step 4: Final commit if any uncommitted changes**

```bash
git status
# If anything uncommitted:
git add .
git commit -m "feat(visualizer): complete kernel flow visualizer"
```

---

## Self-Review

### Spec coverage

| Spec section | Implementing task(s) |
|--------------|----------------------|
| Zero build dependency | Tasks 1-4 (pure HTML/CSS/JS) |
| 10 kernel modules | Tasks 5-14 |
| Step-by-step animation | Tasks 3, 4, 16 |
| Chinese explanations + source links | Tasks 5-14, 15 |
| Register panel updates | Tasks 3, 5-14 |
| Dark minimal lab theme | Tasks 2, 16 |
| Keyboard shortcuts | Task 4 |
| doc/index.md update | Task 17 |

### Placeholder scan

- `:xxx` placeholders exist in Tasks 5-14 for source line numbers. Task 15 is dedicated to replacing them with real values via codegraph.
- No other TBD/TODO/"implement later" patterns.

### Type consistency

- All scenes export `id`, `title`, `initialRegisters`, `svg` (function), and `steps` array with consistent shape.
- `SceneRenderer` and `StepPlayer` signatures remain stable across tasks.

---

## Execution Handoff

**Plan complete and saved to `.claude/plans/2026-06-27-xeros-kernel-visualizer.md`.**

Two execution options:

1. **Subagent-Driven (recommended)** - Dispatch a fresh subagent per task, review between tasks, fast iteration.
2. **Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach do you prefer?
