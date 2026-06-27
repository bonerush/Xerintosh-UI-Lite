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
