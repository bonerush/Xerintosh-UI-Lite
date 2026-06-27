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

export const registersScene = {
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
