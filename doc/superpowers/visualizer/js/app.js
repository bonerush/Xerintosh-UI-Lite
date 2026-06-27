import { SceneRenderer } from './scene-renderer.js';
import { StepPlayer } from './step-player.js';
import { scenes } from './scenes/index.js';

const moduleList = document.getElementById('module-list');
const stageSvg = document.getElementById('stage-svg');
const stageLabel = document.getElementById('stage-label');
const details = {
  step: document.getElementById('details-step'),
  title: document.getElementById('details-title'),
  desc: document.getElementById('details-desc'),
  source: document.getElementById('details-source'),
};
const registerTable = document.getElementById('register-table');
const progressFill = document.getElementById('progress-fill');
const btnPlay = document.getElementById('btn-play');
const btnPrev = document.getElementById('btn-prev');
const btnNext = document.getElementById('btn-next');
const btnReset = document.getElementById('btn-reset');
const speedSelect = document.getElementById('speed');

const renderer = new SceneRenderer(stageSvg, stageLabel, details, registerTable);
const player = new StepPlayer(renderer, (current, total) => {
  progressFill.style.width = `${(current / total) * 100}%`;
});

function renderModuleList() {
  moduleList.innerHTML = '';
  scenes.forEach((scene, index) => {
    const li = document.createElement('li');
    li.className = 'module-item';
    li.textContent = scene.title;
    li.dataset.index = index;
    li.addEventListener('click', () => selectModule(index));
    moduleList.appendChild(li);
  });
}

function selectModule(index) {
  document.querySelectorAll('.module-item').forEach((el, i) => {
    el.classList.toggle('active', i === index);
  });
  player.setScene(scenes[index]);
  updatePlayButton(false);
}

function updatePlayButton(playing) {
  btnPlay.textContent = playing ? '暂停' : '播放';
}

btnPlay.addEventListener('click', () => {
  if (player.playing) {
    player.pause();
    updatePlayButton(false);
  } else {
    player.play();
    updatePlayButton(true);
    const check = setInterval(() => {
      if (!player.playing) {
        updatePlayButton(false);
        clearInterval(check);
      }
    }, 100);
  }
});

btnNext.addEventListener('click', () => {
  player.pause();
  updatePlayButton(false);
  player.next();
});

btnPrev.addEventListener('click', () => {
  player.pause();
  updatePlayButton(false);
  player.prev();
});

btnReset.addEventListener('click', () => {
  player.reset();
  updatePlayButton(false);
});

speedSelect.addEventListener('change', (e) => {
  player.setSpeed(parseInt(e.target.value, 10));
});

document.addEventListener('keydown', (e) => {
  if (e.code === 'Space') {
    e.preventDefault();
    btnPlay.click();
  } else if (e.code === 'ArrowRight') {
    btnNext.click();
  } else if (e.code === 'ArrowLeft') {
    btnPrev.click();
  }
});

renderModuleList();
selectModule(0);
