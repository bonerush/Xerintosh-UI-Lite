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

export const smpIpiScene = {
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
