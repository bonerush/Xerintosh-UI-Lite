import { bootScene } from './boot.js';
import { contextSwitchScene } from './context-switch.js';
import { schedulerScene } from './scheduler.js';
import { smpIpiScene } from './smp-ipi.js';
import { syncScene } from './sync.js';
import { vfsScene } from './vfs.js';
import { interruptScene } from './interrupt.js';
import { memoryScene } from './memory.js';
import { registersScene } from './registers.js';
import { shellScene } from './shell.js';

export const scenes = [
  bootScene,
  contextSwitchScene,
  schedulerScene,
  smpIpiScene,
  syncScene,
  vfsScene,
  interruptScene,
  memoryScene,
  registersScene,
  shellScene,
];
