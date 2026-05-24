/**
 * canvas.js — 画布引擎
 *
 * 负责网格渲染、缩放、鼠标坐标转换、元素绘制、选择高亮。
 */

(function (global) {
    'use strict';

    /* ═══ Canvas Engine ═══ */

    function CanvasEngine(canvasId, options) {
        this.canvas   = document.getElementById(canvasId);
        this.ctx      = this.canvas.getContext('2d');
        this.scale    = options.scale   || 4;
        this.gridSize = options.grid    || 8;
        this.showGrid = options.showGrid !== false;
        this.width    = options.width   || 80;
        this.height   = options.height  || 160;
        this.bgColor  = options.bgColor || '#000000';

        this.elements     = [];
        this.selectedId   = null;
        this.hoveredId    = null;

        // 拖拽状态
        this.dragState = {
            active:     false,
            moving:     false,
            resizing:   false,
            drawing:    false,
            element:    null,
            handle:     null,
            startX:     0,
            startY:     0,
            origX:      0,
            origY:      0,
            origW:      0,
            origH:      0,
            origR:      0,
            origX2:     0,
            origY2:     0,
        };

        this._setupSize();
    }

    /* ── 尺寸设置 ── */

    CanvasEngine.prototype._setupSize = function() {
        this.canvas.width  = this.width  * this.scale;
        this.canvas.height = this.height * this.scale;
        this.canvas.style.width  = this.canvas.width  + 'px';
        this.canvas.style.height = this.canvas.height + 'px';
    };

    CanvasEngine.prototype.setSize = function(w, h) {
        this.width  = w;
        this.height = h;
        this._setupSize();
        this.render();
    };

    CanvasEngine.prototype.setScale = function(s) {
        this.scale = Math.max(1, Math.min(16, Math.round(s)));
        this._setupSize();
        this.render();
    };

    /* ── 坐标转换 ── */

    CanvasEngine.prototype.toLogical = function(clientX, clientY) {
        const rect = this.canvas.getBoundingClientRect();
        const sx   = (clientX - rect.left) / rect.width;
        const sy   = (clientY - rect.top)  / rect.height;
        return {
            x: Math.floor(sx * this.width),
            y: Math.floor(sy * this.height),
        };
    };

    /* ── 主渲染 ── */

    CanvasEngine.prototype.render = function() {
        const ctx = this.ctx;
        const s   = this.scale;
        const w   = this.width;
        const h   = this.height;

        // 清屏
        ctx.fillStyle = this.bgColor;
        ctx.fillRect(0, 0, w * s, h * s);

        // 网格
        if (this.showGrid) {
            this._drawGrid(ctx, s, w, h);
        }

        // 按 layer 排序后绘制元素
        const sorted = [...this.elements].sort((a, b) => (a.layer || 0) - (b.layer || 0));

        ctx.save();
        ctx.scale(s, s);

        for (const el of sorted) {
            if (typeof global.UIElements !== 'undefined') {
                global.UIElements.render(ctx, el);
            }
        }

        // 绘制智能对齐提示线（复用 dragState 中已计算的 guides）
        if (this.dragState.active && (this.dragState.moving || this.dragState.resizing) && this.dragState.snapGuides) {
            if (this.dragState.snapGuides.length > 0) {
                this._drawSnapGuides(ctx, this.dragState.snapGuides, w, h);
            }
        }

        // 绘制选择高亮
        if (this.selectedId) {
            const sel = this.elements.find(e => e.id === this.selectedId);
            if (sel) this._drawSelection(ctx, sel);
        }

        // 绘制悬停预览
        if (this.hoveredId && this.hoveredId !== this.selectedId) {
            const hov = this.elements.find(e => e.id === this.hoveredId);
            if (hov) this._drawHover(ctx, hov);
        }

        // 绘制拖拽预览（绘制中的新形状）
        if (this.dragState.drawing && this.dragState.preview) {
            this._drawPreview(ctx, this.dragState.preview);
        }

        ctx.restore();
    };

    /* ── 网格 ── */

    CanvasEngine.prototype._drawGrid = function(ctx, s, w, h) {
        ctx.strokeStyle = this.gridSize === 1 ? 'rgba(30,30,30,0.5)' : 'rgba(40,40,40,0.6)';
        ctx.lineWidth   = 1;
        ctx.beginPath();

        for (let x = 0; x <= w; x++) {
            const px = x * s;
            const isMajor = x % this.gridSize === 0;
            ctx.strokeStyle = isMajor ? 'rgba(60,60,60,0.5)' : 'rgba(30,30,30,0.3)';
            ctx.beginPath();
            ctx.moveTo(px + 0.5, 0);
            ctx.lineTo(px + 0.5, h * s);
            ctx.stroke();
        }

        for (let y = 0; y <= h; y++) {
            const py = y * s;
            const isMajor = y % this.gridSize === 0;
            ctx.strokeStyle = isMajor ? 'rgba(60,60,60,0.5)' : 'rgba(30,30,30,0.3)';
            ctx.beginPath();
            ctx.moveTo(0, py + 0.5);
            ctx.lineTo(w * s, py + 0.5);
            ctx.stroke();
        }
    };

    /* ── 选择高亮 ── */

    /* ── 智能对齐提示线 ── */

    CanvasEngine.prototype._getSnapEdges = function(el) {
        const type = el.subtype || el.type;
        let left, right, top, bottom, cx, cy;

        if (type === 'circle' || type === 'filled-circle') {
            left   = el.x;
            right  = el.x + el.r * 2;
            top    = el.y;
            bottom = el.y + el.r * 2;
        } else if (type === 'line') {
            left   = Math.min(el.x, el.x2);
            right  = Math.max(el.x, el.x2);
            top    = Math.min(el.y, el.y2);
            bottom = Math.max(el.y, el.y2);
        } else {
            left   = el.x;
            right  = el.x + (el.w || 0);
            top    = el.y;
            bottom = el.y + (el.h || 0);
        }
        cx = (left + right) / 2;
        cy = (top + bottom) / 2;

        return {
            x: [left, cx, right],
            y: [top, cy, bottom],
        };
    };

    CanvasEngine.prototype._computeSnapGuides = function(dragEl, allElements) {
        const SNAP_TOLERANCE = 2; // 逻辑像素容差
        const dragEdges = this._getSnapEdges(dragEl);
        const guides = [];

        for (const other of allElements) {
            if (other.id === dragEl.id) continue;
            const otherEdges = this._getSnapEdges(other);

            for (const dx of dragEdges.x) {
                for (const ox of otherEdges.x) {
                    if (Math.abs(dx - ox) <= SNAP_TOLERANCE) {
                        guides.push({ type: 'x', value: ox });
                    }
                }
            }
            for (const dy of dragEdges.y) {
                for (const oy of otherEdges.y) {
                    if (Math.abs(dy - oy) <= SNAP_TOLERANCE) {
                        guides.push({ type: 'y', value: oy });
                    }
                }
            }
        }

        // 去重
        const seen = new Set();
        return guides.filter(g => {
            const key = g.type + ':' + g.value;
            if (seen.has(key)) return false;
            seen.add(key);
            return true;
        });
    };

    CanvasEngine.prototype._drawSnapGuides = function(ctx, guides, w, h) {
        ctx.save();
        ctx.strokeStyle = '#FFD700';
        ctx.lineWidth   = 0.5;
        ctx.setLineDash([2, 2]);
        for (const g of guides) {
            ctx.beginPath();
            if (g.type === 'x') {
                ctx.moveTo(g.value, 0);
                ctx.lineTo(g.value, h);
            } else {
                ctx.moveTo(0, g.value);
                ctx.lineTo(w, g.value);
            }
            ctx.stroke();
        }
        ctx.restore();
    };

    CanvasEngine.prototype._snapToGuides = function(el, guides) {
        const edges = this._getSnapEdges(el);
        let offsetX = 0;
        let offsetY = 0;
        let snappedX = false;
        let snappedY = false;

        for (const g of guides) {
            if (g.type === 'x' && !snappedX) {
                for (let i = 0; i < edges.x.length; i++) {
                    if (Math.abs(edges.x[i] - g.value) <= 2) {
                        offsetX = g.value - edges.x[i];
                        snappedX = true;
                        break;
                    }
                }
            }
            if (g.type === 'y' && !snappedY) {
                for (let i = 0; i < edges.y.length; i++) {
                    if (Math.abs(edges.y[i] - g.value) <= 2) {
                        offsetY = g.value - edges.y[i];
                        snappedY = true;
                        break;
                    }
                }
            }
        }

        const type = el.subtype || el.type;
        if (offsetX !== 0 || offsetY !== 0) {
            if (type === 'line') {
                el.x += offsetX;
                el.y += offsetY;
                el.x2 += offsetX;
                el.y2 += offsetY;
            } else if (type === 'circle' || type === 'filled-circle') {
                el.x += offsetX;
                el.y += offsetY;
            } else {
                el.x += offsetX;
                el.y += offsetY;
            }
        }
    };

    CanvasEngine.prototype._drawSelection = function(ctx, el) {
        const type = el.subtype || el.type;

        ctx.save();
        ctx.strokeStyle = '#FFD700';
        ctx.lineWidth   = 1;
        ctx.setLineDash([2, 2]);

        if (type === 'line') {
            ctx.beginPath();
            ctx.moveTo(el.x, el.y);
            ctx.lineTo(el.x2, el.y2);
            ctx.stroke();
            ctx.restore();
            this._drawHandle(ctx, el.x,  el.y);
            this._drawHandle(ctx, el.x2, el.y2);
            return;
        }

        if (type === 'circle' || type === 'filled-circle') {
            ctx.strokeRect(el.x, el.y, el.r * 2, el.r * 2);
            ctx.restore();
            this._drawHandle(ctx, el.x,         el.y);
            this._drawHandle(ctx, el.x + el.r*2, el.y);
            this._drawHandle(ctx, el.x,         el.y + el.r*2);
            this._drawHandle(ctx, el.x + el.r*2, el.y + el.r*2);
            return;
        }

        // 矩形类通用
        ctx.strokeRect(el.x, el.y, el.w, el.h);
        ctx.restore();

        // 8 个手柄：4 角（圆形）+ 4 边中点（方形）
        this._drawHandle(ctx, el.x,         el.y);
        this._drawEdgeHandle(ctx, el.x + el.w / 2, el.y);
        this._drawHandle(ctx, el.x + el.w,   el.y);
        this._drawEdgeHandle(ctx, el.x,         el.y + el.h / 2);
        this._drawEdgeHandle(ctx, el.x + el.w,   el.y + el.h / 2);
        this._drawHandle(ctx, el.x,         el.y + el.h);
        this._drawEdgeHandle(ctx, el.x + el.w / 2, el.y + el.h);
        this._drawHandle(ctx, el.x + el.w,   el.y + el.h);
    };

    CanvasEngine.prototype._drawHover = function(ctx, el) {
        const type = el.subtype || el.type;

        ctx.save();
        ctx.strokeStyle = 'rgba(255,255,255,0.3)';
        ctx.lineWidth   = 1;
        ctx.setLineDash([2, 2]);

        if (type === 'line') {
            ctx.beginPath();
            ctx.moveTo(el.x, el.y);
            ctx.lineTo(el.x2, el.y2);
            ctx.stroke();
        } else if (type === 'circle' || type === 'filled-circle') {
            ctx.strokeRect(el.x, el.y, el.r * 2, el.r * 2);
        } else {
            ctx.strokeRect(el.x, el.y, el.w, el.h);
        }

        ctx.restore();
    };

    CanvasEngine.prototype._drawHandle = function(ctx, x, y) {
        ctx.fillStyle   = '#FFD700';
        ctx.strokeStyle = '#000';
        ctx.lineWidth   = 1;
        const r = 2.5;
        ctx.beginPath();
        ctx.arc(x, y, r, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();
    };

    CanvasEngine.prototype._drawEdgeHandle = function(ctx, x, y) {
        ctx.fillStyle   = '#FFD700';
        ctx.strokeStyle = '#000';
        ctx.lineWidth   = 1;
        const s = 2.5;
        ctx.beginPath();
        ctx.moveTo(x, y - s);
        ctx.lineTo(x + s, y);
        ctx.lineTo(x, y + s);
        ctx.lineTo(x - s, y);
        ctx.closePath();
        ctx.fill();
        ctx.stroke();
    };

    /* ── 绘制预览 ── */

    CanvasEngine.prototype._drawPreview = function(ctx, el) {
        ctx.globalAlpha = 0.5;
        if (typeof global.UIElements !== 'undefined') {
            global.UIElements.render(ctx, el);
        }
        ctx.globalAlpha = 1.0;
    };

    /* ── 元素管理 ── */

    CanvasEngine.prototype.addElement = function(el) {
        this.elements.push(el);
        this.render();
        return el;
    };

    CanvasEngine.prototype.removeElement = function(id) {
        const idx = this.elements.findIndex(e => e.id === id);
        if (idx >= 0) {
            this.elements.splice(idx, 1);
            if (this.selectedId === id) this.selectedId = null;
            if (this.hoveredId === id)  this.hoveredId  = null;
            this.render();
            return true;
        }
        return false;
    };

    CanvasEngine.prototype.getElement = function(id) {
        return this.elements.find(e => e.id === id);
    };

    CanvasEngine.prototype.setSelected = function(id) {
        this.selectedId = id || null;
        this.render();
    };

    CanvasEngine.prototype.clear = function() {
        this.elements = [];
        this.selectedId = null;
        this.hoveredId = null;
        this.render();
    };

    /* ── 命中检测（从后往前，顶层优先）─ */

    CanvasEngine.prototype.hitTest = function(x, y) {
        for (let i = this.elements.length - 1; i >= 0; i--) {
            const el = this.elements[i];
            if (typeof global.UIElements !== 'undefined') {
                if (global.UIElements.hitTest(el, x, y)) {
                    return el;
                }
            }
        }
        return null;
    };

    CanvasEngine.prototype.hitHandle = function(x, y) {
        if (!this.selectedId) return null;
        const el = this.getElement(this.selectedId);
        if (!el || typeof global.UIElements === 'undefined') return null;
        return global.UIElements.hitHandle(el, x, y);
    };

    CanvasEngine.prototype.getHandleCursor = function(handleName) {
        switch (handleName) {
            case 'top': case 'bottom': return 'ns-resize';
            case 'left': case 'right': return 'ew-resize';
            case 'top-left': case 'bottom-right': return 'nwse-resize';
            case 'top-right': case 'bottom-left': return 'nesw-resize';
            case 'start': case 'end': return 'move';
            default: return 'default';
        }
    };

    /* ── 图层操作 ── */

    CanvasEngine.prototype.moveLayer = function(id, direction) {
        const idx = this.elements.findIndex(e => e.id === id);
        if (idx < 0) return false;
        const el = this.elements[idx];
        const newLayer = Math.max(0, (el.layer || 0) + direction);
        if (newLayer !== el.layer) {
            el.layer = newLayer;
            this.render();
            return true;
        }
        return false;
    };

    /* ── 橡皮擦 ── */

    CanvasEngine.prototype.eraseAt = function(x, y) {
        const hit = this.hitTest(x, y);
        if (hit) {
            this.removeElement(hit.id);
            return true;
        }
        return false;
    };

    /* ═══ 导出 ═══ */

    global.CanvasEngine = CanvasEngine;

})(window);
