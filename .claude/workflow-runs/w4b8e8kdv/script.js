export const meta = {
  name: 'm5stick-to-espidf-migration-plan',
  description: '分析 M5Stick-P1 (Arduino) 并设计迁移到 esp-idf 的完整计划',
  phases: [
    { title: 'Explore', detail: '并行分析源项目各层与目标项目结构' },
    { title: 'Map', detail: 'Arduino/esp-idf API 与库映射' },
    { title: 'Plan', detail: '综合生成详细迁移计划' }
  ]
}

const source = args.sourceProject
const target = args.targetProject

phase('Explore')
log(`开始分析源项目 ${source} 与目标项目 ${target}`)

const exploreResults = await parallel([
  () => agent(`分析 ${source}/platformio.ini 与构建系统。列出所有 env、framework、board、lib_deps、build_flags、partition 表，并指出哪些配置在迁移到 esp-idf 时必须更改。返回结构化摘要。`, {
    label: 'build-config',
    phase: 'Explore',
    schema: {
      type: 'object',
      properties: {
        framework: { type: 'string' },
        board: { type: 'string' },
        libraries: { type: 'array', items: { type: 'string' } },
        buildFlags: { type: 'array', items: { type: 'string' } },
        partitionTable: { type: 'string' },
        migrationNotes: { type: 'array', items: { type: 'string' } }
      },
      required: ['framework','board','libraries','migrationNotes']
    }
  }),
  () => agent(`分析 ${source}/src/hal/ 目录下所有文件。列出 HAL 模块、它们使用的 Arduino/M5Unified API、硬件外设（GPIO/I2C/SPI/屏幕/电源/输入等），并评估迁移到 esp-idf 所需的替换难度。`, {
    label: 'hal-layer',
    phase: 'Explore',
    schema: {
      type: 'object',
      properties: {
        modules: { type: 'array', items: { type: 'string' } },
        arduinoApis: { type: 'array', items: { type: 'string' } },
        peripherals: { type: 'array', items: { type: 'string' } },
        difficulty: { type: 'string', enum: ['low','medium','high'] },
        notes: { type: 'array', items: { type: 'string' } }
      },
      required: ['modules','arduinoApis','peripherals','difficulty','notes']
    }
  }),
  () => agent(`分析 ${source}/src/kernel/ 目录。列出核心模块（任务调度、VFS、devfs、procfs、shell、MPU、同步等）、与 FreeRTOS/Arduino 的耦合点、以及迁移到 esp-idf FreeRTOS 时需要注意的 API 差异。`, {
    label: 'kernel-layer',
    phase: 'Explore',
    schema: {
      type: 'object',
      properties: {
        modules: { type: 'array', items: { type: 'string' } },
        freertosCoupling: { type: 'array', items: { type: 'string' } },
        arduinoCoupling: { type: 'array', items: { type: 'string' } },
        difficulty: { type: 'string', enum: ['low','medium','high'] },
        notes: { type: 'array', items: { type: 'string' } }
      },
      required: ['modules','freertosCoupling','arduinoCoupling','difficulty','notes']
    }
  }),
  () => agent(`分析 ${source}/src/ui/ 与 ${source}/src/app/ 目录。列出 UI 框架、应用模块、它们对 M5GFX/M5Unified 的依赖、屏幕分辨率/颜色假设、字体资源，以及迁移策略建议。`, {
    label: 'ui-app-layer',
    phase: 'Explore',
    schema: {
      type: 'object',
      properties: {
        uiModules: { type: 'array', items: { type: 'string' } },
        appModules: { type: 'array', items: { type: 'string' } },
        gfxDependencies: { type: 'array', items: { type: 'string' } },
        screenAssumptions: { type: 'array', items: { type: 'string' } },
        difficulty: { type: 'string', enum: ['low','medium','high'] },
        notes: { type: 'array', items: { type: 'string' } }
      },
      required: ['uiModules','appModules','gfxDependencies','difficulty','notes']
    }
  }),
  () => agent(`分析目标项目 ${target}。列出项目结构、platformio.ini、src/、CMakeLists.txt、sdkconfig，评估它作为 esp-idf 迁移目标的基础是否完整，还缺少什么。`, {
    label: 'target-project',
    phase: 'Explore',
    schema: {
      type: 'object',
      properties: {
        structure: { type: 'array', items: { type: 'string' } },
        framework: { type: 'string' },
        readyForMigration: { type: 'boolean' },
        missingPieces: { type: 'array', items: { type: 'string' } }
      },
      required: ['structure','framework','readyForMigration','missingPieces']
    }
  })
])

log(`探索完成：HAL 难度 ${exploreResults[1]?.difficulty}, Kernel 难度 ${exploreResults[2]?.difficulty}, UI/App 难度 ${exploreResults[3]?.difficulty}`)

phase('Map')
log('开始映射 Arduino/M5Unified API 到 esp-idf 替代方案')

const mapResults = await parallel([
  () => agent(`基于前序 HAL 分析，为每个 Arduino/M5Unified API（GPIO、I2C、SPI、delay、Serial、电源管理、屏幕初始化等）给出 esp-idf 等价 API 或推荐组件。返回 API 映射表。`, {
    label: 'api-map',
    phase: 'Map',
    schema: {
      type: 'object',
      properties: {
        mappings: {
          type: 'array',
          items: {
            type: 'object',
            properties: {
              arduinoApi: { type: 'string' },
              espIdfReplacement: { type: 'string' },
              notes: { type: 'string' }
            },
            required: ['arduinoApi','espIdfReplacement','notes']
          }
        }
      },
      required: ['mappings']
    }
  }),
  () => agent(`M5GFX 在 esp-idf 下的替代方案评估：可选 lvgl、自定义 framebuffer、esp-idf 自带组件，或其他轻量图形库。给出推荐方案及理由，包括与 ST7735/ST7789 屏幕驱动的兼容性。`, {
    label: 'gfx-alternatives',
    phase: 'Map',
    schema: {
      type: 'object',
      properties: {
        options: {
          type: 'array',
          items: {
            type: 'object',
            properties: {
              name: { type: 'string' },
              pros: { type: 'array', items: { type: 'string' } },
              cons: { type: 'array', items: { type: 'string' } },
              recommendation: { type: 'string' }
            },
            required: ['name','pros','cons','recommendation']
          }
        },
        chosen: { type: 'string' }
      },
      required: ['options','chosen']
    }
  }),
  () => agent(`M5Unified 功能（按钮、电源 PMIC AXP192、IMU MPU6886、RTC、Speaker、LED 等）在 esp-idf 下的替代库或驱动推荐。给出每个外设需要引入的组件或自行实现的建议。`, {
    label: 'm5unified-alternatives',
    phase: 'Map',
    schema: {
      type: 'object',
      properties: {
        peripherals: {
          type: 'array',
          items: {
            type: 'object',
            properties: {
              feature: { type: 'string' },
              espIdfApproach: { type: 'string' },
              library: { type: 'string' },
              notes: { type: 'string' }
            },
            required: ['feature','espIdfApproach','library','notes']
          }
        }
      },
      required: ['peripherals']
    }
  })
])

phase('Plan')
log('综合生成迁移计划')

const plan = await agent(`综合以下所有信息，生成一份完整的 M5Stick-P1 (Arduino/M5Unified) → xerintosh (esp-idf) 迁移计划：

源项目：${source}
目标项目：${target}

探索结果：
${JSON.stringify(exploreResults.filter(Boolean), null, 2)}

映射结果：
${JSON.stringify(mapResults.filter(Boolean), null, 2)}

要求：
1. 按阶段列出迁移步骤（准备、HAL 重写、Kernel 适配、UI/App 适配、构建系统迁移、验证）。
2. 每个阶段列出需要修改/新增/删除的具体文件。
3. 识别高风险模块和回滚策略。
4. 给出推荐的图形库和依赖清单。
5. 给出 CMakeLists.txt、sdkconfig、platformio.ini 的变更要点。
6. 给出测试验证策略（native 测试是否保留、目标板测试要点）。

返回结构化的迁移计划。`, {
  label: 'migration-plan',
  phase: 'Plan',
  schema: {
    type: 'object',
    properties: {
      summary: { type: 'string' },
      phases: {
        type: 'array',
        items: {
          type: 'object',
          properties: {
            name: { type: 'string' },
            steps: { type: 'array', items: { type: 'string' } },
            files: { type: 'array', items: { type: 'string' } },
            risks: { type: 'array', items: { type: 'string' } }
          },
          required: ['name','steps','files']
        }
      },
      dependencies: { type: 'array', items: { type: 'string' } },
      highRiskModules: { type: 'array', items: { type: 'string' } },
      verificationStrategy: { type: 'array', items: { type: 'string' } }
    },
    required: ['summary','phases','dependencies','highRiskModules','verificationStrategy']
  }
})

return { exploreResults, mapResults, plan }