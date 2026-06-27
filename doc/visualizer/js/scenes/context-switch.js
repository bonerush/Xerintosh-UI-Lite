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
