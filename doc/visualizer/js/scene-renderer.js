export class SceneRenderer {
  constructor(stageSvg, stageLabel, details, registerTable) {
    this.stageSvg = stageSvg;
    this.stageLabel = stageLabel;
    this.details = details;
    this.registerTable = registerTable;
    this.currentScene = null;
  }

  load(scene) {
    this.currentScene = scene;
    this.stageLabel.textContent = `${scene.title} · Step 0 / ${scene.steps.length}`;
    this.stageSvg.innerHTML = '';
    this.stageSvg.appendChild(scene.svg());
    this.details.title.textContent = scene.title;
    this.details.step.textContent = '';
    this.details.desc.textContent = '点击「播放」或「下一步」开始。';
    this.details.source.innerHTML = '';
    this.renderRegisters(scene.initialRegisters || {});
  }

  renderStep(step, index, total) {
    this.stageLabel.textContent = `${this.currentScene.title} · Step ${index + 1} / ${total}`;

    const doneIds = new Set();
    for (let i = 0; i < index; i++) {
      const past = this.currentScene.steps[i];
      if (past.highlight) {
        past.highlight.forEach(id => doneIds.add(id));
      }
    }

    this.stageSvg.querySelectorAll('[data-node]').forEach(node => {
      const id = node.getAttribute('data-node');
      node.classList.remove('active', 'done', 'dim');
      if (step.highlight.includes(id)) {
        node.classList.add('active');
      } else if (doneIds.has(id)) {
        node.classList.add('done');
      } else if (step.dim?.includes(id)) {
        node.classList.add('dim');
      }
    });

    this.stageSvg.querySelectorAll('[data-arrow]').forEach(arrow => {
      const id = arrow.getAttribute('data-arrow');
      if (step.arrows.includes(id)) {
        arrow.classList.add('flow');
      } else {
        arrow.classList.remove('flow');
      }
    });

    this.details.step.textContent = step.label;
    this.details.title.textContent = step.title || this.currentScene.title;
    this.details.desc.textContent = step.detail;
    if (step.source) {
      this.details.source.innerHTML = `<span>Source: </span><a href="../../${step.source.split(':')[0]}" target="_blank">${step.source}</a>`;
    } else {
      this.details.source.innerHTML = '';
    }

    this.animateDetails();
    this.renderRegisters(step.registerChange || {});
  }

  animateDetails() {
    const el = this.details.root;
    el.classList.remove('step-enter');
    void el.offsetWidth;
    el.classList.add('step-enter');
  }

  renderRegisters(changes) {
    const base = this.currentScene?.initialRegisters || {};
    const registers = { ...base, ...changes };
    this.registerTable.innerHTML = Object.entries(registers)
      .map(([name, value]) => {
        const changed = name in changes;
        return `<div class="register-row ${changed ? 'changed' : ''}">
          <span class="register-name">${name}</span>
          <span class="register-value">${value}</span>
        </div>`;
      })
      .join('');
  }
}
