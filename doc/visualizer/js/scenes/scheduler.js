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

export const schedulerScene = {
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
