(function() {

// === SceneRenderer ===
class SceneRenderer {
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

    const doneIds = new Set();
    for (let i = 0; i < index; i++) {
      const past = this.currentScene.steps[i];
      if (past.highlight) {
        past.highlight.forEach(id => doneIds.add(id));
      }
    }

    this.stageSvg.querySelectorAll('[data-node]').forEach(node => {
      const id = node.getAttribute('data-node');
      node.classList.remove('active', 'done', 'dim');
      if (step.highlight.includes(id)) {
        node.classList.add('active');
      } else if (doneIds.has(id)) {
        node.classList.add('done');
      } else if (step.dim?.includes(id)) {
        node.classList.add('dim');
      }
    });

    this.stageSvg.querySelectorAll('[data-arrow]').forEach(arrow => {
      const id = arrow.getAttribute('data-arrow');
      if (step.arrows.includes(id)) {
        arrow.classList.add('flow');
      } else {
        arrow.classList.remove('flow');
      }
    });

    this.details.step.textContent = step.label;
    this.details.title.textContent = step.title || this.currentScene.title;
    this.details.desc.textContent = step.detail;
    if (step.source) {
      this.details.source.innerHTML = `<span>Source: </span><a href="../../${step.source.split(':')[0]}" target="_blank">${step.source}</a>`;
    } else {
      this.details.source.innerHTML = '';
    }

    this.animateDetails();
    this.renderRegisters(step.registerChange || {});
  }

  animateDetails() {
    const el = this.details.root;
    el.classList.remove('step-enter');
    requestAnimationFrame(() => {
      el.classList.add('step-enter');
    });
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

// === StepPlayer ===
class StepPlayer {
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

// === Scene: boot ===
function createBootSvg() {
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

const bootScene = {
  id: 'boot',
  title: '系统启动流程',
  initialRegisters: {
    PC: '0x40000400',
    SP: '0x3FFC0000',
    PS: '0x0006001E',
    A0: '0x00000000',
  },
  svg: createBootSvg,
  steps: [
    {
      id: 'reset-vector',
      label: '从复位向量启动',
      highlight: ['reset'],
      arrows: [],
      detail: 'ESP32 上电后从 ROM 复位向量开始执行，初始化堆栈并跳转到入口代码。',
      source: 'src/kernel/esp32/ctx_switch.S:195-210',
    },
    {
      id: 'hw-init',
      label: '初始化硬件',
      highlight: ['hw-init'],
      arrows: ['reset-to-hw'],
      detail: '配置时钟、UART、中断控制器、看门狗和 GPIO，为内核运行准备硬件环境。',
      source: 'src/kernel/kern_init.c:36-55',
    },
    {
      id: 'kernel-init',
      label: '初始化内核子系统',
      highlight: ['kernel-init'],
      arrows: ['hw-to-kernel'],
      detail: '依次初始化调度器、内存分配器、VFS、定时器、同步原语和 SMP。',
      source: 'src/kernel/kern_init.c:36-55',
    },
    {
      id: 'create-init-task',
      label: '创建第一个任务',
      highlight: ['create-init'],
      arrows: ['kernel-to-create'],
      detail: '创建 init 任务，作为用户空间的第一个入口。',
      source: 'src/kernel/kern_task_lifecycle.c:27-120',
    },
    {
      id: 'start-scheduler',
      label: '启动调度器',
      highlight: ['schedule'],
      arrows: ['create-to-schedule'],
      detail: '打开 tick 定时器，触发第一次上下文切换，开始多任务调度。',
      source: 'src/kernel/kern_sched.c:245-310',
    },
  ],
};

// === Scene: context-switch ===
function createContextSwitchSvg() {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 800 400');
  svg.innerHTML = `
    <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#71717a" />
    </marker>

    <g data-node="current">
      <rect x="40" y="160" width="120" height="50" rx="6" />
      <text x="100" y="190" text-anchor="middle">当前任务</text>
    </g>

    <g data-node="save">
      <rect x="220" y="160" width="120" height="50" rx="6" />
      <text x="280" y="190" text-anchor="middle">保存上下文</text>
    </g>

    <g data-node="pick">
      <rect x="400" y="160" width="120" height="50" rx="6" />
      <text x="460" y="190" text-anchor="middle">选择下一个</text>
    </g>

    <g data-node="restore">
      <rect x="580" y="160" width="120" height="50" rx="6" />
      <text x="640" y="190" text-anchor="middle">恢复上下文</text>
    </g>

    <path data-arrow="current-to-save" d="M 160 185 L 210 185" marker-end="url(#arrowhead)" />
    <path data-arrow="save-to-pick" d="M 340 185 L 390 185" marker-end="url(#arrowhead)" />
    <path data-arrow="pick-to-restore" d="M 520 185 L 570 185" marker-end="url(#arrowhead)" />
  `;
  return svg;
}

const contextSwitchScene = {
  id: 'context-switch',
  title: '上下文切换',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createContextSwitchSvg,
  steps: [
    {
      id: 'current-task',
      label: '触发调度',
      highlight: ['current'],
      arrows: [],
      detail: '当前任务时间片耗尽或主动让出 CPU，触发调度器。',
      source: 'src/kernel/kern_sched.c:479-506',
    },
    {
      id: 'save-context',
      label: '保存寄存器上下文',
      highlight: ['save'],
      arrows: ['current-to-save'],
      detail: '将当前任务的寄存器（PC、SP、PS、A0-A15）保存到其 TCB 中。',
      source: 'src/kernel/esp32/ctx_switch.S:454-516',
    },
    {
      id: 'pick-next',
      label: '选择下一个任务',
      highlight: ['pick'],
      arrows: ['save-to-pick'],
      detail: '调度器从就绪队列中选择最高优先级的任务。',
      source: 'src/kernel/kern_sched_class.c:30-50',
    },
    {
      id: 'restore-context',
      label: '恢复新任务上下文',
      highlight: ['restore'],
      arrows: ['pick-to-restore'],
      detail: '从新任务的 TCB 中恢复寄存器，跳转到其 PC 继续执行。',
      source: 'src/kernel/esp32/ctx_switch.S:520-566',
    },
  ],
};

// === Scene: scheduler ===
function createSchedulerSvg() {
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

const schedulerScene = {
  id: 'scheduler',
  title: '调度器流程',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createSchedulerSvg,
  steps: [
    {
      id: 'tick-entry',
      label: 'Tick 中断入口',
      highlight: ['tick'],
      arrows: [],
      detail: '定时器产生周期中断，进入 tick handler。',
      source: 'src/kernel/kern_timer.c:61-79',
    },
    {
      id: 'update-timeslice',
      label: '递减当前任务时间片',
      highlight: ['timeslice'],
      arrows: ['tick-to-slice'],
      detail: '如果当前任务时间片耗尽，标记需要重新调度。',
      source: 'src/kernel/kern_sched.c:479-505',
    },
    {
      id: 'scan-buckets',
      label: '扫描优先级桶',
      highlight: ['buckets'],
      arrows: ['slice-to-buckets'],
      detail: '从最高优先级开始遍历就绪队列桶，找到非空桶。',
      source: 'src/kernel/kern_sched_class.c:30-50',
    },
    {
      id: 'select-task',
      label: '选择下一个任务',
      highlight: ['select'],
      arrows: ['buckets-to-select'],
      detail: '从最高优先级桶中取出队首任务作为下一个运行任务。',
      source: 'src/kernel/kern_sched.c:506-536',
    },
  ],
};

// === Scene: smp-ipi ===
function createSmpIpiSvg() {
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

const smpIpiScene = {
  id: 'smp-ipi',
  title: 'SMP 双核调度与 IPI',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createSmpIpiSvg,
  steps: [
    {
      id: 'per-cpu-sched',
      label: '每个 CPU 独立调度',
      highlight: ['cpu0', 'cpu1'],
      arrows: [],
      detail: 'CPU0 和 CPU1 各自维护自己的就绪队列和当前任务。',
      source: 'src/kernel/kern_smp.c:48-63',
    },
    {
      id: 'cpu0-ipi',
      label: 'CPU0 发送 IPI',
      highlight: ['cpu0', 'ipi'],
      arrows: ['cpu0-to-ipi'],
      detail: '当 CPU0 唤醒一个绑定在 CPU1 上的任务时，触发跨核中断通知 CPU1。',
      source: 'src/kernel/kern_smp.c:104-120',
    },
    {
      id: 'cpu1-receive',
      label: 'CPU1 接收 IPI',
      highlight: ['cpu1', 'ipi'],
      arrows: ['cpu1-to-ipi'],
      detail: 'CPU1 进入 IPI handler，检查是否有高优先级任务需要立即调度。',
      source: 'src/kernel/esp32/smp_ipi.S:31-45',
    },
    {
      id: 'migrate-wake',
      label: '任务迁移或唤醒',
      highlight: ['migrate'],
      arrows: ['ipi-to-migrate'],
      detail: 'CPU1 将被唤醒的任务加入自己的就绪队列，必要时触发上下文切换。',
      source: 'src/kernel/kern_smp.c:104-120',
    },
  ],
};

// === Scene: sync ===
function createSyncSvg() {
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

const syncScene = {
  id: 'sync',
  title: '同步原语流程',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createSyncSvg,
  steps: [
    {
      id: 'task-a-lock',
      label: '任务 A 尝试获取锁',
      highlight: ['task-a', 'lock'],
      arrows: ['a-to-lock'],
      detail: '任务 A 调用 mutex_lock / spinlock_lock，尝试进入临界区。',
      source: 'src/kernel/kern_sync.c:23-30',
    },
    {
      id: 'task-b-block',
      label: '任务 B 阻塞等待',
      highlight: ['task-b', 'wait-queue'],
      arrows: ['b-to-lock', 'lock-to-wait'],
      detail: '锁已被占用，任务 B 被放入等待队列并触发调度。',
      source: 'src/kernel/kern_sync.c:47-82',
    },
    {
      id: 'task-a-unlock',
      label: '任务 A 释放锁',
      highlight: ['task-a', 'lock'],
      arrows: [],
      detail: '任务 A 离开临界区，释放锁，并检查等待队列。',
      source: 'src/kernel/kern_sync.c:83-101',
    },
    {
      id: 'wake-b',
      label: '唤醒任务 B',
      highlight: ['wait-queue', 'task-b'],
      arrows: ['wait-to-lock'],
      detail: '从等待队列取出任务 B，标记为就绪状态。若 B 优先级更高，触发优先级继承或调度。',
      source: 'src/kernel/kern_sync.c:83-101',
    },
  ],
};

// === Scene: vfs ===
function createVfsSvg() {
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

const vfsScene = {
  id: 'vfs',
  title: 'VFS 调用流程',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createVfsSvg,
  steps: [
    {
      id: 'sys-call',
      label: '应用发起系统调用',
      highlight: ['app'],
      arrows: [],
      detail: '应用调用 open/read/write/close 等文件操作。',
      source: 'src/kernel/kern_vfs.c:510-531',
    },
    {
      id: 'vfs-dispatch',
      label: 'VFS 统一入口',
      highlight: ['vfs'],
      arrows: ['app-to-vfs'],
      detail: 'VFS 层根据文件路径解析文件描述符，进入 inode 查找流程。',
      source: 'src/kernel/kern_vfs.c:510-531',
    },
    {
      id: 'inode-lookup',
      label: 'inode 查找',
      highlight: ['inode'],
      arrows: ['vfs-to-inode'],
      detail: '根据路径逐层查找 dentry 和 inode，确定文件类型（普通/设备/特殊）。',
      source: 'src/kernel/kern_vfs.c:233-272',
    },
    {
      id: 'device-file',
      label: '设备文件分发',
      highlight: ['dev'],
      arrows: ['inode-to-dev'],
      detail: '如果是设备文件（如 /dev/ttyS0），根据主次设备号找到对应驱动。',
      source: 'src/kernel/kern_vfs.c:592-607',
    },
    {
      id: 'driver-callback',
      label: '驱动回调执行',
      highlight: ['driver'],
      arrows: ['dev-to-driver'],
      detail: '调用驱动注册的 read/write 回调，完成实际硬件操作或数据返回。',
      source: 'src/kernel/kern_vfs.c:608-623',
    },
  ],
};

// === Scene: interrupt ===
function createInterruptSvg() {
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

    <g data-node="return">
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

const interruptScene = {
  id: 'interrupt',
  title: '中断与异常处理',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createInterruptSvg,
  steps: [
    {
      id: 'irq-signal',
      label: '中断信号到达',
      highlight: ['irq'],
      arrows: [],
      detail: '外部设备或定时器触发中断，CPU 暂停当前指令流。',
      source: 'src/kernel/kern_isr.c:15-33',
    },
    {
      id: 'save-context',
      label: '中断入口保存上下文',
      highlight: ['entry'],
      arrows: ['irq-to-entry'],
      detail: '进入汇编中断入口，保存 PC、PS、A0-A15 等到中断栈。',
      source: 'src/kernel/esp32/ctx_switch.S:454-516',
      registerChange: { SP: '0x3FFC7F00', PS: '0x0006001F' },
    },
    {
      id: 'dispatch-isr',
      label: '分发 ISR',
      highlight: ['dispatch'],
      arrows: ['entry-to-dispatch'],
      detail: '根据中断号查找中断描述符表，调用注册的 C 语言 ISR。',
      source: 'src/kernel/kern_isr.c:15-33',
    },
    {
      id: 'sched-check',
      label: '调度检查',
      highlight: ['sched-check'],
      arrows: ['dispatch-to-sched'],
      detail: 'ISR 返回前检查是否需要重新调度（如 tick handler 减时间片）。',
      source: 'src/kernel/kern_sched.c:479-505',
    },
    {
      id: 'restore-return',
      label: '恢复上下文并返回',
      highlight: ['return'],
      arrows: ['sched-to-return'],
      detail: '恢复寄存器，执行 rfi 或 retw 返回被中断的任务或新任务。',
      source: 'src/kernel/esp32/ctx_switch.S:520-566',
      registerChange: { SP: '0x3FFC8000', PS: '0x0006001E' },
    },
  ],
};

// === Scene: memory ===
function createMemorySvg() {
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

const memoryScene = {
  id: 'memory',
  title: '内存分配流程',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createMemorySvg,
  steps: [
    {
      id: 'kmalloc-call',
      label: '调用 kmalloc',
      highlight: ['kmalloc'],
      arrows: [],
      detail: '内核代码请求分配指定大小的内存块。',
      source: 'src/kernel/kern_kmalloc.c:102-110',
    },
    {
      id: 'scan-free-list',
      label: '遍历空闲链表',
      highlight: ['free-list'],
      arrows: ['kmalloc-to-free'],
      detail: '从空闲链表头开始查找第一个足够大的空闲块。',
      source: 'src/kernel/kern_kmalloc.c:68-101',
    },
    {
      id: 'split-block',
      label: '分割空闲块',
      highlight: ['split'],
      arrows: ['free-to-split'],
      detail: '如果空闲块远大于请求大小，将其分割为已分配块和剩余空闲块。',
      source: 'src/kernel/kern_kmalloc.c:68-101',
    },
    {
      id: 'return-pointer',
      label: '返回内存指针',
      highlight: ['allocated'],
      arrows: ['split-to-alloc'],
      detail: '返回指向已分配内存的指针，并更新空闲链表。',
      source: 'src/kernel/kern_kmalloc.c:102-110',
    },
    {
      id: 'kfree-merge',
      label: 'kfree 释放与合并',
      highlight: ['kfree'],
      arrows: ['alloc-to-kfree'],
      detail: '释放内存时将其插回空闲链表，并尝试与相邻空闲块合并。',
      source: 'src/kernel/kern_kmalloc.c:133-152',
    },
  ],
};

// === Scene: registers ===
function createRegistersSvg() {
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

const registersScene = {
  id: 'registers',
  title: '寄存器配置流程',
  initialRegisters: {
    PC: '0x40000400',
    SP: '0x3FFC0000',
    PS: '0x0006001E',
    'CPENABLE': '0x00000000',
  },
  svg: createRegistersSvg,
  steps: [
    {
      id: 'cpenable',
      label: '配置协处理器使能',
      highlight: ['cp0'],
      arrows: [],
      detail: '设置 CPENABLE 位，启用 FPU 等协处理器访问权限。',
      source: 'src/kernel/kern_init.c:36-55',
      registerChange: { CPENABLE: '0x00000001' },
    },
    {
      id: 'watchdog',
      label: '配置看门狗',
      highlight: ['wdt'],
      arrows: ['cp0-to-wdt'],
      detail: '初始化看门狗定时器，设置超时周期与喂狗机制。',
      source: 'src/kernel/kern_init.c:36-55',
    },
    {
      id: 'timer',
      label: '配置系统定时器',
      highlight: ['timer'],
      arrows: ['wdt-to-timer'],
      detail: '配置 Xtensa 定时器产生周期性 tick 中断。',
      source: 'src/kernel/kern_timer.c:61-79',
    },
    {
      id: 'gpio-matrix',
      label: '配置 GPIO 矩阵',
      highlight: ['gpio'],
      arrows: ['timer-to-gpio'],
      detail: '将外设信号映射到物理 GPIO 引脚，如 SPI、UART。',
      source: 'src/kernel/kern_gpiofs.c:296-320',
    },
    {
      id: 'interrupt',
      label: '使能中断控制器',
      highlight: ['int'],
      arrows: ['gpio-to-int'],
      detail: '配置中断优先级与中断向量表，最终打开全局中断。',
      source: 'src/kernel/kern_isr.c:15-33',
    },
  ],
};

// === Scene: shell ===
function createShellSvg() {
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

const shellScene = {
  id: 'shell',
  title: 'Shell 执行流程',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createShellSvg,
  steps: [
    {
      id: 'uart-input',
      label: '串口接收字符',
      highlight: ['uart'],
      arrows: [],
      detail: 'Shell 任务从串口逐字符读取用户输入，直到收到换行。',
      source: 'src/kernel/kern_shell.c:471-495',
    },
    {
      id: 'line-buffer',
      label: '填充命令缓冲区',
      highlight: ['buffer'],
      arrows: ['uart-to-buffer'],
      detail: '将完整命令行存入缓冲区，等待解析。',
      source: 'src/kernel/kern_shell.c:594-619',
    },
    {
      id: 'parse-args',
      label: '解析命令与参数',
      highlight: ['parse'],
      arrows: ['buffer-to-parse'],
      detail: '按空格分割命令名和参数，处理引号与转义。',
      source: 'src/kernel/kern_shell_parser.c:58-130',
    },
    {
      id: 'lookup-command',
      label: '查找命令表',
      highlight: ['lookup'],
      arrows: ['parse-to-lookup'],
      detail: '遍历内置命令表，匹配命令名并获取回调函数指针。',
      source: 'src/kernel/kern_shell_cmds.c:1165-1177',
    },
    {
      id: 'execute-output',
      label: '执行并输出',
      highlight: ['execute'],
      arrows: ['lookup-to-execute'],
      detail: '调用命令回调，将结果写回串口缓冲区输出。',
      source: 'src/kernel/kern_shell_cmds.c:1179-1193',
    },
  ],
};

// === scenes array ===
const scenes = [bootScene, contextSwitchScene, schedulerScene, smpIpiScene, syncScene, vfsScene, interruptScene, memoryScene, registersScene, shellScene];

// === app.js ===

const moduleList = document.getElementById('module-list');
const stageSvg = document.getElementById('stage-svg');
const stageLabel = document.getElementById('stage-label');
const details = {
  root: document.getElementById('details'),
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

})();
