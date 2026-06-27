export class StepPlayer {
  constructor(renderer, onProgress) {
    this.renderer = renderer;
    this.onProgress = onProgress;
    this.scene = null;
    this.current = -1;
    this.playing = false;
    this.timer = null;
    this.speed = 1000;
  }

  setScene(scene) {
    this.scene = scene;
    this.current = -1;
    this.playing = false;
    this.stopTimer();
    this.renderer.load(scene);
    this.onProgress(0, scene.steps.length);
  }

  play() {
    if (this.playing) return;
    this.playing = true;
    if (this.current >= this.scene.steps.length - 1) {
      this.current = -1;
    }
    this.step();
  }

  pause() {
    this.playing = false;
    this.stopTimer();
  }

  next() {
    if (this.current < this.scene.steps.length - 1) {
      this.current++;
      this.render();
    }
  }

  prev() {
    if (this.current > 0) {
      this.current--;
      this.render();
    }
  }

  reset() {
    this.pause();
    this.current = -1;
    this.renderer.load(this.scene);
    this.onProgress(0, this.scene.steps.length);
  }

  step() {
    if (!this.playing) return;
    this.next();
    if (this.current < this.scene.steps.length - 1) {
      this.timer = setTimeout(() => this.step(), this.speed);
    } else {
      this.playing = false;
    }
  }

  setSpeed(ms) {
    this.speed = ms;
  }

  stopTimer() {
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = null;
    }
  }

  render() {
    const step = this.scene.steps[this.current];
    this.renderer.renderStep(step, this.current, this.scene.steps.length);
    this.onProgress(this.current + 1, this.scene.steps.length);
  }
}
