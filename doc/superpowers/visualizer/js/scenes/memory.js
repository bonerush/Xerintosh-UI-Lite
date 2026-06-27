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
