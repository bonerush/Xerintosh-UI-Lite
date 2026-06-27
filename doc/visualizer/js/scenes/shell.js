function createSvg() {
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

export const shellScene = {
  id: 'shell',
  title: 'Shell 执行流程',
  initialRegisters: {
    PC: '0x40081200',
    SP: '0x3FFC8000',
    PS: '0x0006001E',
  },
  svg: createSvg,
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
