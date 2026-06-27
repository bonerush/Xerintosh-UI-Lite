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
