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
