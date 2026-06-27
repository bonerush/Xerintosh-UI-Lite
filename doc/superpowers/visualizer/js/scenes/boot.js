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
