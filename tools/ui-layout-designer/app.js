/**
 * app.js — 应用主逻辑
 *
 * 状态管理、工具切换、事件绑定、属性面板联动、文件操作。
 */

/* ═══ i18n ═══ */

const i18n = {
    en: {
        device: 'Device:',
        custom: 'Custom...',
        portrait: 'Portrait',
        landscape: 'Landscape',
        grid: 'Grid',
        new: 'New',
        import: 'Import',
        export_json: 'Export JSON',
        export_llm: 'Export LLM Prompt',
        tools: 'Tools',
        standard_icons: 'Standard Icons',
        extra_icons: 'Extra Icons',
        controls: 'Controls',
        colors: 'Colors',
        fill_shape: 'Fill Shape',
        white: 'White',
        black: 'Black',
        accent_green: 'Accent (Green)',
        red: 'Red',
        blue: 'Blue',
        elements: 'Elements',
        properties: 'Properties',
        type: 'Type',
        id: 'ID',
        x: 'X',
        y: 'Y',
        width: 'Width',
        height: 'Height',
        radius: 'Radius',
        text: 'Text',
        font_size: 'Font Size',
        label: 'Label',
        color: 'Color',
        layer: 'Layer',
        delete: 'Delete',
        duplicate: 'Duplicate',
        shortcuts: 'Shortcuts',
        enter_text: 'Enter Text',
        enter_text_ph: 'Enter text...',
        cancel: 'Cancel',
        text_color: 'Text Color',
        ok: 'OK',
        custom_screen_size: 'Custom Screen Size',
        llm_prompt: 'LLM Prompt',
        copy_clipboard: 'Copy to Clipboard',
        close: 'Close',
        confirm_clear: 'Clear all elements? This cannot be undone.',
        import_success: 'Layout imported successfully!',
        import_failed: 'Import failed: ',
        copied: 'Copied!',
        element: 'element',
        elements: 'elements',
    },
    zh: {
        device: '设备:',
        custom: '自定义...',
        portrait: '竖屏',
        landscape: '横屏',
        grid: '网格',
        new: '新建',
        import: '导入',
        export_json: '导出 JSON',
        export_llm: '导出 LLM 提示词',
        tools: '工具',
        standard_icons: '标准图标',
        extra_icons: '扩展图标',
        controls: '控件',
        colors: '颜色',
        fill_shape: '填充形状',
        white: '白色',
        black: '黑色',
        accent_green: '强调色 (绿)',
        red: '红色',
        blue: '蓝色',
        elements: '元素',
        properties: '属性',
        type: '类型',
        id: 'ID',
        x: 'X',
        y: 'Y',
        width: '宽度',
        height: '高度',
        radius: '半径',
        text: '文字',
        font_size: '字体大小',
        label: '标签',
        color: '颜色',
        layer: '层级',
        delete: '删除',
        duplicate: '复制',
        shortcuts: '快捷键',
        enter_text: '输入文字',
        enter_text_ph: '请输入文字...',
        cancel: '取消',
        text_color: '文字颜色',
        ok: '确定',
        custom_screen_size: '自定义屏幕尺寸',
        llm_prompt: 'LLM 提示词',
        copy_clipboard: '复制到剪贴板',
        close: '关闭',
        confirm_clear: '清空所有元素？此操作不可撤销。',
        import_success: '布局导入成功！',
        import_failed: '导入失败：',
        copied: '已复制！',
        element: '个元素',
        elements: '个元素',
    },
};

let currentLang = localStorage.getItem('ui-lang') || 'zh';

function t(key) {
    return (i18n[currentLang] && i18n[currentLang][key]) || i18n.en[key] || key;
}

function applyLanguage(lang) {
    currentLang = lang;
    localStorage.setItem('ui-lang', lang);

    // 更新 data-i18n 元素
    document.querySelectorAll('[data-i18n]').forEach(el => {
        const key = el.getAttribute('data-i18n');
        if (i18n[lang][key]) {
            el.textContent = i18n[lang][key];
        }
    });

    // 更新 data-i18n-placeholder 元素
    document.querySelectorAll('[data-i18n-placeholder]').forEach(el => {
        const key = el.getAttribute('data-i18n-placeholder');
        if (i18n[lang][key]) {
            el.placeholder = i18n[lang][key];
        }
    });

    // 更新 data-i18n-title 元素
    document.querySelectorAll('[data-i18n-title]').forEach(el => {
        const key = el.getAttribute('data-i18n-title');
        if (i18n[lang][key]) {
            el.title = i18n[lang][key];
        }
    });

    // 更新动态内容
    const btn = document.getElementById('toggle-orientation');
    if (btn) {
        const isPortrait = btn.textContent === t('portrait') || btn.textContent === i18n.en.portrait || btn.textContent === i18n.zh.portrait;
        btn.textContent = isPortrait ? t('portrait') : t('landscape');
    }

    // 更新导出复制按钮
    const copyBtn = document.getElementById('export-copy');
    if (copyBtn && copyBtn.textContent !== t('copied')) {
        copyBtn.textContent = t('copy_clipboard');
    }

    // 更新语言切换按钮
    const langBtn = document.getElementById('lang-switch');
    if (langBtn) {
        langBtn.textContent = lang === 'zh' ? 'EN' : '中';
    }
}

document.addEventListener('DOMContentLoaded', function () {
    'use strict';

    /* ═══ 状态 ═══ */

    const state = {
        tool:        'select',
        color:       '#FFFFFF',
        fill:        false,
        screenW:     80,
        screenH:     160,
        device:      'M5Stick',
        zoom:        4,
        showGrid:    true,
        orientation: 'portrait',
        history:     [],
        historyIdx:  -1,
        meta:        { device: 'M5Stick', width: 80, height: 160, orientation: 'portrait' },
    };

    /* ═══ DOM 引用 ═══ */

    const $ = id => document.getElementById(id);

    const canvasContainer = $('canvas-container');
    const mouseCoords     = $('mouse-coords');
    const elementCount    = $('element-count');
    const canvasSize      = $('canvas-size');

    /* ═══ 初始化画布 ═══ */

    const engine = new CanvasEngine('design-canvas', {
        width:    state.screenW,
        height:   state.screenH,
        scale:    state.zoom,
        showGrid: state.showGrid,
    });

    /* ═══ 历史记录（Undo/Redo）═══ */

    function saveHistory() {
        // 丢弃当前指针之后的历史
        state.history = state.history.slice(0, state.historyIdx + 1);
        state.history.push(JSON.stringify(engine.elements));
        // 限制 50 步
        if (state.history.length > 50) {
            state.history.shift();
        } else {
            state.historyIdx++;
        }
    }

    function undo() {
        if (state.historyIdx > 0) {
            state.historyIdx--;
            engine.elements = JSON.parse(state.history[state.historyIdx]);
            engine.selectedId = null;
            engine.render();
            updateElementList();
            updatePropertiesPanel();
            updateStatus();
        }
    }

    function redo() {
        if (state.historyIdx < state.history.length - 1) {
            state.historyIdx++;
            engine.elements = JSON.parse(state.history[state.historyIdx]);
            engine.selectedId = null;
            engine.render();
            updateElementList();
            updatePropertiesPanel();
            updateStatus();
        }
    }

    // 初始快照
    saveHistory();

    /* ═══ 工具切换 ═══ */

    function setTool(name) {
        state.tool = name;
        document.querySelectorAll('.tool-btn').forEach(btn => {
            btn.classList.toggle('active', btn.dataset.tool === name);
        });
        engine.canvas.className = 'tool-' + name;
    }

    document.querySelectorAll('.tool-btn').forEach(btn => {
        btn.addEventListener('click', () => setTool(btn.dataset.tool));
    });

    /* ═══ 颜色选择 ═══ */

    document.querySelectorAll('.color-swatch').forEach(swatch => {
        swatch.addEventListener('click', () => {
            document.querySelectorAll('.color-swatch').forEach(s => s.classList.remove('active'));
            swatch.classList.add('active');
            state.color = swatch.dataset.color;
            if (swatch.dataset.color === 'custom') {
                state.color = $('custom-color').value;
            }
        });
    });

    $('custom-color').addEventListener('input', e => {
        state.color = e.target.value;
        document.querySelectorAll('.color-swatch').forEach(s => s.classList.remove('active'));
    });

    $('fill-toggle').addEventListener('change', e => {
        state.fill = e.target.checked;
    });

    /* ═══ 缩放 ═══ */

    $('zoom-in').addEventListener('click', () => {
        state.zoom++;
        engine.setScale(state.zoom);
        $('zoom-level').textContent = state.zoom + 'x';
    });

    $('zoom-out').addEventListener('click', () => {
        if (state.zoom > 1) {
            state.zoom--;
            engine.setScale(state.zoom);
            $('zoom-level').textContent = state.zoom + 'x';
        }
    });

    $('toggle-grid').addEventListener('click', () => {
        state.showGrid = !state.showGrid;
        engine.showGrid = state.showGrid;
        $('toggle-grid').classList.toggle('active', state.showGrid);
        engine.render();
    });

    /* ═══ 设备切换 ═══ */

    $('device-select').addEventListener('change', e => {
        const val = e.target.value;
        if (val === 'custom') {
            $('custom-size-modal').classList.add('active');
            return;
        }
        const [w, h, dev] = val.split(',');
        changeDevice(parseInt(w), parseInt(h), dev);
    });

    function changeDevice(w, h, dev) {
        if (state.orientation === 'landscape') {
            [w, h] = [h, w];
        }
        state.screenW = w;
        state.screenH = h;
        state.device  = dev;
        state.meta    = { device: dev, width: w, height: h, orientation: state.orientation };
        engine.setSize(w, h);
        $('screen-dims').textContent = `${w} × ${h}`;
        canvasSize.textContent = `Canvas: ${w} × ${h}`;
        // 清空画布
        engine.clear();
        saveHistory();
        updateElementList();
        updatePropertiesPanel();
        updateStatus();
    }

    function toggleOrientation() {
        state.orientation = state.orientation === 'portrait' ? 'landscape' : 'portrait';
        const btn = $('toggle-orientation');
        btn.textContent = state.orientation === 'portrait' ? t('portrait') : t('landscape');
        btn.classList.toggle('active', state.orientation === 'portrait');
        // 重新应用当前设备的尺寸（会自动根据方向交换）
        const sel = $('device-select');
        if (sel.value !== 'custom') {
            const [w, h, dev] = sel.value.split(',');
            changeDevice(parseInt(w), parseInt(h), dev);
        } else {
            changeDevice(state.screenH, state.screenW, state.device);
        }
    }

    $('toggle-orientation').addEventListener('click', toggleOrientation);

    $('custom-size-ok').addEventListener('click', () => {
        const w = parseInt($('custom-width').value) || 80;
        const h = parseInt($('custom-height').value) || 160;
        changeDevice(w, h, 'Custom');
        $('custom-size-modal').classList.remove('active');
    });

    $('custom-size-cancel').addEventListener('click', () => {
        $('custom-size-modal').classList.remove('active');
        const val = `${state.screenW},${state.screenH},${state.device}`;
        $('device-select').value = val;
    });

    /* ═══ 图标拖拽 ═══ */

    document.querySelectorAll('.icon-item').forEach(item => {
        item.addEventListener('mousedown', e => {
            const iconName = item.dataset.icon;
            const el = UIElements.create('icon', {
                x: Math.floor(state.screenW / 2) - 4,
                y: Math.floor(state.screenH / 2) - 4,
                w: 8,
                h: 8,
                iconName: iconName,
                color: state.color,
            });
            engine.addElement(el);
            engine.setSelected(el.id);
            saveHistory();
            updateElementList();
            updatePropertiesPanel();
            updateStatus();
            e.preventDefault();
        });
    });

    /* ═══ 控件拖拽 ═══ */

    document.querySelectorAll('.control-item').forEach(item => {
        item.addEventListener('mousedown', e => {
            const subtype = item.dataset.control;
            const labelMap = {
                list_item:   'Item',
                switch_item: 'Switch',
                slider_item: 'Slider',
                button_item: 'Button',
                user_item:   'App',
            };
            const el = UIElements.create('control', {
                x: 4,
                y: Math.floor(state.screenH / 2) - 9,
                w: state.screenW - 8,
                h: 18,
                subtype: subtype,
                content: labelMap[subtype] || '',
                color: state.color,
            });
            engine.addElement(el);
            engine.setSelected(el.id);
            saveHistory();
            updateElementList();
            updatePropertiesPanel();
            updateStatus();
            e.preventDefault();
        });
    });

    /* ═══ 画布鼠标事件 ═══ */

    let isMouseDown = false;
    let dragEl = null;

    engine.canvas.addEventListener('mousedown', e => {
        isMouseDown = true;
        const pos = engine.toLogical(e.clientX, e.clientY);
        const handle = engine.hitHandle(pos.x, pos.y);

        if (handle) {
            // 开始调整大小
            const el = engine.getElement(engine.selectedId);
            if (!el) return;
            engine.dragState = {
                active:   true,
                resizing: true,
                handle:   handle,
                element:  el,
                startX:   pos.x,
                startY:   pos.y,
                origX:    el.x,
                origY:    el.y,
                origW:    el.w || 0,
                origH:    el.h || 0,
                origR:    el.r || 0,
                origX2:   el.x2 || 0,
                origY2:   el.y2 || 0,
            };
            return;
        }

        if (state.tool === 'select') {
            const hit = engine.hitTest(pos.x, pos.y);
            if (hit) {
                engine.setSelected(hit.id);
                dragEl = hit;
                engine.dragState = {
                    active: true,
                    moving: true,
                    element: hit,
                    startX: pos.x,
                    startY: pos.y,
                    origX:  hit.x,
                    origY:  hit.y,
                };
            } else {
                engine.setSelected(null);
                dragEl = null;
            }
            updateElementList();
            updatePropertiesPanel();
        } else if (state.tool === 'text') {
            $('text-modal').classList.add('active');
            $('text-input').value = '';
            $('text-input').focus();
            state.pendingTextPos = pos;
        } else if (state.tool === 'eraser') {
            if (engine.eraseAt(pos.x, pos.y)) {
                saveHistory();
                updateElementList();
                updatePropertiesPanel();
                updateStatus();
            }
        } else {
            // 开始绘制新形状
            const shapeType = state.fill && state.tool === 'rectangle' ? 'filled-rect'
                            : state.fill && state.tool === 'round-rect' ? 'filled-round-rect'
                            : state.fill && state.tool === 'circle' ? 'filled-circle'
                            : state.tool;
            const el = UIElements.create(shapeType, {
                x: pos.x,
                y: pos.y,
                w: 1,
                h: 1,
                color: state.color,
                fill: state.fill,
            });
            engine.dragState = {
                active:  true,
                drawing: true,
                element: el,
                preview: el,
                startX:  pos.x,
                startY:  pos.y,
            };
        }
    });

    /* ═══ 双击就地文本编辑 ═══ */

    engine.canvas.addEventListener('dblclick', e => {
        if (state.tool !== 'select') return;
        const pos = engine.toLogical(e.clientX, e.clientY);
        const hit = engine.hitTest(pos.x, pos.y);
        if (!hit) return;

        engine.setSelected(hit.id);
        updateElementList();
        updatePropertiesPanel();

        const rect = engine.canvas.getBoundingClientRect();
        const scale = engine.scale;
        let inputX, inputY, inputW, inputH;

        const type = hit.subtype || hit.type;
        if (type === 'text') {
            inputX = rect.left + hit.x * scale;
            inputY = rect.top + hit.y * scale;
            inputW = Math.max(60, (hit.w || 20) * scale);
            inputH = Math.max(20, (hit.h || 12) * scale);
        } else {
            inputX = rect.left + hit.x * scale;
            inputY = rect.top + hit.y * scale;
            inputW = Math.max(60, (hit.w || 20) * scale);
            inputH = Math.max(20, (hit.h || 12) * scale);
        }

        const input = document.createElement('input');
        input.type = 'text';
        input.value = hit.text || '';
        input.style.cssText = `
            position: fixed;
            left: ${inputX}px;
            top: ${inputY}px;
            width: ${inputW}px;
            height: ${inputH}px;
            font-family: "Share Tech Mono", monospace;
            font-size: ${Math.max(12, (hit.fontSize || 12) * scale / 4)}px;
            color: ${hit.textColor || hit.color};
            background: rgba(0,0,0,0.7);
            border: 1px solid ${hit.textColor || hit.color};
            outline: none;
            padding: 2px 4px;
            z-index: 10000;
            box-sizing: border-box;
        `;

        function finishEdit(save) {
            if (!input.parentNode) return;
            if (save) {
                hit.text = input.value;
                engine.render();
                updatePropertiesPanel();
                saveHistory();
            }
            input.remove();
        }

        input.addEventListener('keydown', ev => {
            if (ev.key === 'Enter') { finishEdit(true); ev.preventDefault(); }
            if (ev.key === 'Escape') { finishEdit(false); ev.preventDefault(); }
        });
        input.addEventListener('blur', () => finishEdit(true));

        document.body.appendChild(input);
        input.focus();
        input.select();
    });

    window.addEventListener('mousemove', e => {
        const pos = engine.toLogical(e.clientX, e.clientY);
        mouseCoords.textContent = `x: ${pos.x}, y: ${pos.y}`;

        // 悬停检测
        if (!engine.dragState.active) {
            let cursor = '';
            if (state.tool === 'select') {
                const handle = engine.hitHandle(pos.x, pos.y);
                if (handle) {
                    cursor = engine.getHandleCursor(handle);
                } else {
                    const hit = engine.hitTest(pos.x, pos.y);
                    if (hit) {
                        engine.hoveredId = hit.id;
                        cursor = 'pointer';
                    } else if (engine.hoveredId) {
                        engine.hoveredId = null;
                        engine.render();
                    }
                }
            }
            if (engine.canvas.style.cursor !== cursor) {
                engine.canvas.style.cursor = cursor;
            }
        }

        if (!isMouseDown || !engine.dragState.active) return;

        const ds = engine.dragState;
        const dx = pos.x - ds.startX;
        const dy = pos.y - ds.startY;

        if (ds.moving && ds.element) {
            ds.element.x = Math.max(0, Math.min(state.screenW - (ds.element.w || 1), ds.origX + dx));
            ds.element.y = Math.max(0, Math.min(state.screenH - (ds.element.h || 1), ds.origY + dy));
            if (ds.element.subtype === 'line') {
                ds.element.x2 = ds.element.x + (ds.origX2 - ds.origX);
                ds.element.y2 = ds.element.y + (ds.origY2 - ds.origY);
            }
            // 智能对齐吸附（缓存 guides 供 render 复用）
            ds.snapGuides = engine._computeSnapGuides(ds.element, engine.elements);
            if (ds.snapGuides.length > 0) {
                engine._snapToGuides(ds.element, ds.snapGuides);
            }
            engine.render();
            updatePropertiesPanel();
        } else if (ds.resizing && ds.element) {
            const el = ds.element;
            const type = el.subtype || el.type;

            if (type === 'line') {
                if (ds.handle === 'start') { el.x = pos.x; el.y = pos.y; }
                else if (ds.handle === 'end') { el.x2 = pos.x; el.y2 = pos.y; }
            } else if (type === 'circle' || type === 'filled-circle') {
                if (ds.handle === 'bottom-right') {
                    const r = Math.max(1, Math.floor(Math.max(dx, dy) / 2));
                    el.r = r;
                }
            } else {
                if (ds.handle === 'top-left') {
                    const nx = Math.min(ds.origX + dx, ds.origX + ds.origW - 1);
                    const ny = Math.min(ds.origY + dy, ds.origY + ds.origH - 1);
                    el.w = ds.origX + ds.origW - nx;
                    el.h = ds.origY + ds.origH - ny;
                    el.x = nx;
                    el.y = ny;
                } else if (ds.handle === 'top-right') {
                    const ny = Math.min(ds.origY + dy, ds.origY + ds.origH - 1);
                    el.w = Math.max(1, ds.origW + dx);
                    el.h = ds.origY + ds.origH - ny;
                    el.y = ny;
                } else if (ds.handle === 'bottom-left') {
                    const nx = Math.min(ds.origX + dx, ds.origX + ds.origW - 1);
                    el.w = ds.origX + ds.origW - nx;
                    el.h = Math.max(1, ds.origH + dy);
                    el.x = nx;
                } else if (ds.handle === 'bottom-right') {
                    el.w = Math.max(1, ds.origW + dx);
                    el.h = Math.max(1, ds.origH + dy);
                } else if (ds.handle === 'top') {
                    const ny = Math.min(ds.origY + dy, ds.origY + ds.origH - 1);
                    el.h = ds.origY + ds.origH - ny;
                    el.y = ny;
                } else if (ds.handle === 'bottom') {
                    el.h = Math.max(1, ds.origH + dy);
                } else if (ds.handle === 'left') {
                    const nx = Math.min(ds.origX + dx, ds.origX + ds.origW - 1);
                    el.w = ds.origX + ds.origW - nx;
                    el.x = nx;
                } else if (ds.handle === 'right') {
                    el.w = Math.max(1, ds.origW + dx);
                }
            }
            engine.render();
            updatePropertiesPanel();
        } else if (ds.drawing && ds.preview) {
            const el = ds.preview;
            const type = el.subtype || el.type;

            if (type === 'line') {
                el.x2 = pos.x;
                el.y2 = pos.y;
            } else if (type === 'circle' || type === 'filled-circle') {
                const r = Math.floor(Math.max(Math.abs(dx), Math.abs(dy)) / 2);
                el.r = Math.max(1, r);
            } else {
                const nx = Math.min(ds.startX, pos.x);
                const ny = Math.min(ds.startY, pos.y);
                el.x = nx;
                el.y = ny;
                el.w = Math.abs(pos.x - ds.startX) + 1;
                el.h = Math.abs(pos.y - ds.startY) + 1;
            }
            engine.render();
        }
    });

    window.addEventListener('mouseup', () => {
        if (!isMouseDown) return;
        isMouseDown = false;

        if (engine.dragState.drawing && engine.dragState.preview) {
            const el = engine.dragState.preview;
            // 确保有效尺寸
            const type = el.subtype || el.type;
            if (type === 'line') {
                if (el.x2 === undefined) { el.x2 = el.x + 1; el.y2 = el.y; }
            } else if (type === 'circle' || type === 'filled-circle') {
                if (!el.r || el.r < 1) el.r = 1;
            } else {
                if (el.w < 1) el.w = 1;
                if (el.h < 1) el.h = 1;
            }
            engine.addElement(el);
            engine.setSelected(el.id);
            saveHistory();
            updateElementList();
            updatePropertiesPanel();
            updateStatus();
        } else if (engine.dragState.moving || engine.dragState.resizing) {
            saveHistory();
        }

        engine.dragState = { active: false };
        dragEl = null;
    });

    /* ═══ 文字输入弹窗 ═══ */

    $('text-ok').addEventListener('click', () => {
        const text = $('text-input').value;
        if (text && state.pendingTextPos) {
            const el = UIElements.create('text', {
                x: state.pendingTextPos.x,
                y: state.pendingTextPos.y,
                text: text,
                fontSize: parseInt($('prop-font-size')?.value || 8),
                color: state.color,
            });
            engine.addElement(el);
            engine.setSelected(el.id);
            saveHistory();
            updateElementList();
            updatePropertiesPanel();
            updateStatus();
        }
        $('text-modal').classList.remove('active');
        state.pendingTextPos = null;
    });

    $('text-cancel').addEventListener('click', () => {
        $('text-modal').classList.remove('active');
        state.pendingTextPos = null;
    });

    $('text-input').addEventListener('keydown', e => {
        if (e.key === 'Enter') $('text-ok').click();
        if (e.key === 'Escape') $('text-cancel').click();
    });

    /* ═══ 剪贴板 ═══ */

    let clipboardElement = null;

    async function copySelected() {
        const el = engine.selectedId ? engine.getElement(engine.selectedId) : null;
        if (!el) return;
        const copy = JSON.parse(JSON.stringify(el));
        delete copy.id;
        clipboardElement = copy;
        try {
            await navigator.clipboard.writeText(JSON.stringify(copy));
        } catch (err) {
            // 降级：仅使用内存剪贴板
        }
    }

    async function pasteElement() {
        let data = clipboardElement;
        if (!data) {
            try {
                const text = await navigator.clipboard.readText();
                data = JSON.parse(text);
            } catch (err) {
                return;
            }
        }
        if (!data) return;
        const pasted = UIElements.create(data.type || data.subtype || 'rect', {
            ...data,
            x: data.x + 5,
            y: data.y + 5,
        });
        engine.addElement(pasted);
        engine.setSelected(pasted.id);
        saveHistory();
        updateElementList();
        updatePropertiesPanel();
        updateStatus();
    }

    function duplicateSelected() {
        const el = engine.selectedId ? engine.getElement(engine.selectedId) : null;
        if (!el) return;
        const clone = UIElements.clone(el);
        engine.addElement(clone);
        engine.setSelected(clone.id);
        saveHistory();
        updateElementList();
        updatePropertiesPanel();
        updateStatus();
    }

    /* ═══ 键盘快捷键 ═══ */

    window.addEventListener('keydown', e => {
        // 忽略在输入框中的按键（但允许 Escape）
        if ((e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') && e.key !== 'Escape') return;

        // Escape: 关闭弹窗或取消选择
        if (e.key === 'Escape') {
            const modals = document.querySelectorAll('.modal.active');
            if (modals.length > 0) {
                modals.forEach(m => m.classList.remove('active'));
            } else {
                engine.setSelected(null);
                updateElementList();
                updatePropertiesPanel();
            }
            e.preventDefault();
            return;
        }

        // 工具切换
        const keyMap = {
            'v': 'select', 'r': 'rect', 'f': 'filled-rect',
            'c': 'circle', 'l': 'line', 't': 'text', 'e': 'eraser',
        };
        if (keyMap[e.key.toLowerCase()]) {
            setTool(keyMap[e.key.toLowerCase()]);
            e.preventDefault();
            return;
        }

        // Undo / Redo
        if (e.ctrlKey && e.key.toLowerCase() === 'z') {
            e.preventDefault();
            if (e.shiftKey) {
                redo();
            } else {
                undo();
            }
            return;
        }
        if (e.ctrlKey && e.key.toLowerCase() === 'y') {
            e.preventDefault();
            redo();
            return;
        }

        // Copy / Paste / Duplicate
        if (e.ctrlKey && e.key.toLowerCase() === 'c') {
            e.preventDefault();
            copySelected();
            return;
        }
        if (e.ctrlKey && e.key.toLowerCase() === 'v') {
            e.preventDefault();
            pasteElement();
            return;
        }
        if (e.ctrlKey && e.key.toLowerCase() === 'd') {
            e.preventDefault();
            duplicateSelected();
            return;
        }

        // Select All
        if (e.ctrlKey && e.key.toLowerCase() === 'a') {
            e.preventDefault();
            if (engine.elements.length > 0) {
                engine.setSelected(engine.elements[engine.elements.length - 1].id);
                updateElementList();
                updatePropertiesPanel();
            }
            return;
        }

        // 删除
        if (e.key === 'Delete' || e.key === 'Backspace') {
            if (engine.selectedId) {
                engine.removeElement(engine.selectedId);
                saveHistory();
                updateElementList();
                updatePropertiesPanel();
                updateStatus();
            }
            e.preventDefault();
            return;
        }

        // 方向键微调
        if (engine.selectedId) {
            const el = engine.getElement(engine.selectedId);
            if (!el) return;
            const step = e.shiftKey ? 5 : 1;
            switch (e.key) {
                case 'ArrowUp':    el.y = Math.max(0, el.y - step); break;
                case 'ArrowDown':  el.y = Math.min(state.screenH - 1, el.y + step); break;
                case 'ArrowLeft':  el.x = Math.max(0, el.x - step); break;
                case 'ArrowRight': el.x = Math.min(state.screenW - 1, el.x + step); break;
                default: return;
            }
            engine.render();
            updatePropertiesPanel();
            saveHistory();
            e.preventDefault();
        }
    });

    /* ═══ 属性面板 ═══ */

    const propInputs = {
        id:   $('prop-id'),
        type: $('prop-type'),
        x:    $('prop-x'),
        y:    $('prop-y'),
        w:    $('prop-w'),
        h:    $('prop-h'),
        r:    $('prop-r'),
        text: $('prop-text'),
        font: $('prop-font-size'),
        label: $('prop-label'),
        fill: $('prop-fill'),
        color: $('prop-color'),
        textColor: $('prop-text-color'),
        layer: $('prop-layer'),
    };

    function updatePropertiesPanel() {
        const el = engine.selectedId ? engine.getElement(engine.selectedId) : null;
        const panel = $('properties-panel');

        if (!el) {
            panel.style.opacity = '0.5';
            for (const key in propInputs) {
                if (propInputs[key]) {
                    if (propInputs[key].type === 'checkbox') {
                        propInputs[key].checked = false;
                    } else {
                        propInputs[key].value = '';
                    }
                }
            }
            $('prop-color-hex').textContent = '';
            return;
        }

        panel.style.opacity = '1';
        const type = el.subtype || el.type;

        propInputs.type.value   = type;
        propInputs.id.value     = el.id;
        propInputs.x.value      = el.x;
        propInputs.y.value      = el.y;
        propInputs.color.value  = el.color;
        $('prop-color-hex').textContent = el.color;
        propInputs.textColor.value = el.textColor || el.color;
        $('prop-text-color-hex').textContent = el.textColor || el.color;
        propInputs.layer.value  = el.layer || 0;

        // 显示/隐藏特定属性
        $('prop-w').value = el.w || '';
        $('prop-h').value = el.h || '';
        $('prop-radius-group').style.display = (el.r !== undefined) ? 'block' : 'none';
        if (el.r !== undefined) $('prop-r').value = el.r;

        // 所有元素都支持 text 属性
        $('prop-text-group').style.display = 'block';
        $('prop-text').value = el.text || '';

        $('prop-font-group').style.display = 'block';
        $('prop-font-size').value = el.fontSize || 8;

        $('prop-label-group').style.display = (el.type === 'control') ? 'block' : 'none';
        if (el.type === 'control') $('prop-label').value = el.content || '';

        // Fill 复选框：仅对支持 fill 的形状显示
        const hasFill = (el.fill !== undefined);
        $('prop-fill-group').style.display = hasFill ? 'block' : 'none';
        if (hasFill) $('prop-fill').checked = !!el.fill;
    }

    function bindPropInput(input, key, isNumber) {
        if (!input) return;
        input.addEventListener('input', () => {
            const el = engine.selectedId ? engine.getElement(engine.selectedId) : null;
            if (!el) return;
            let val = input.value;
            if (isNumber) {
                val = parseInt(val);
                if (isNaN(val)) return;
            }
            el[key] = val;
            engine.render();
            // 不立即保存历史，在 blur 时保存
        });
        input.addEventListener('change', () => {
            saveHistory();
            updateElementList();
        });
    }

    bindPropInput($('prop-x'), 'x', true);
    bindPropInput($('prop-y'), 'y', true);
    bindPropInput($('prop-w'), 'w', true);
    bindPropInput($('prop-h'), 'h', true);
    bindPropInput($('prop-r'), 'r', true);
    bindPropInput($('prop-text'), 'text', false);
    bindPropInput($('prop-font-size'), 'fontSize', true);
    bindPropInput($('prop-label'), 'content', false);
    bindPropInput($('prop-color'), 'color', false);
    bindPropInput($('prop-text-color'), 'textColor', false);

    $('prop-color').addEventListener('input', () => {
        $('prop-color-hex').textContent = $('prop-color').value;
    });
    $('prop-text-color').addEventListener('input', () => {
        $('prop-text-color-hex').textContent = $('prop-text-color').value;
    });

    $('prop-fill').addEventListener('change', () => {
        const el = engine.selectedId ? engine.getElement(engine.selectedId) : null;
        if (!el) return;
        el.fill = $('prop-fill').checked;
        engine.render();
        saveHistory();
    });

    /* ═══ 属性面板按钮 ═══ */

    $('layer-up').addEventListener('click', () => {
        if (engine.selectedId && engine.moveLayer(engine.selectedId, 1)) {
            saveHistory();
            updatePropertiesPanel();
            updateElementList();
        }
    });

    $('layer-down').addEventListener('click', () => {
        if (engine.selectedId && engine.moveLayer(engine.selectedId, -1)) {
            saveHistory();
            updatePropertiesPanel();
            updateElementList();
        }
    });

    $('btn-delete').addEventListener('click', () => {
        if (engine.selectedId) {
            engine.removeElement(engine.selectedId);
            saveHistory();
            updateElementList();
            updatePropertiesPanel();
            updateStatus();
        }
    });

    $('btn-duplicate').addEventListener('click', () => {
        const el = engine.selectedId ? engine.getElement(engine.selectedId) : null;
        if (!el) return;
        const clone = UIElements.clone(el);
        engine.addElement(clone);
        engine.setSelected(clone.id);
        saveHistory();
        updateElementList();
        updatePropertiesPanel();
        updateStatus();
    });

    /* ═══ 元素列表 ═══ */

    function updateElementList() {
        const list = $('element-list');
        list.innerHTML = '';
        const sorted = [...engine.elements].sort((a, b) => (a.layer || 0) - (b.layer || 0));
        sorted.forEach(el => {
            const item = document.createElement('div');
            item.className = 'element-item' + (el.id === engine.selectedId ? ' selected' : '');
            item.innerHTML = `
                <span class="el-name">${UIElements.getDisplayName(el)}</span>
                <span class="el-type">${el.subtype || el.type}</span>
            `;
            item.addEventListener('click', () => {
                engine.setSelected(el.id);
                updateElementList();
                updatePropertiesPanel();
            });
            list.appendChild(item);
        });
    }

    /* ═══ 文件操作 ═══ */

    $('btn-new').addEventListener('click', () => {
        if (confirm(t('confirm_clear'))) {
            engine.clear();
            saveHistory();
            updateElementList();
            updatePropertiesPanel();
            updateStatus();
        }
    });

    $('btn-export').addEventListener('click', () => {
        const json = UIExporter.toJSON(engine.elements, state.meta);
        const filename = `layout_${state.device}_${Date.now()}.json`;
        UIExporter.downloadFile(json, filename, 'application/json');
    });

    $('btn-export-llm').addEventListener('click', () => {
        const prompt = UIExporter.toLLMPrompt(engine.elements, state.meta);
        $('export-textarea').value = prompt;
        $('export-modal').classList.add('active');
    });

    $('export-copy').addEventListener('click', () => {
        $('export-textarea').select();
        document.execCommand('copy');
        $('export-copy').textContent = t('copied');
        setTimeout(() => $('export-copy').textContent = t('copy_clipboard'), 1500);
    });

    $('export-close').addEventListener('click', () => {
        $('export-modal').classList.remove('active');
    });

    $('btn-import').addEventListener('click', () => {
        $('import-file').click();
    });

    $('import-file').addEventListener('change', e => {
        const file = e.target.files[0];
        if (!file) return;
        const reader = new FileReader();
        reader.onload = ev => {
            try {
                const data = UIExporter.fromJSON(ev.target.result);
                engine.clear();
                if (data.meta) {
                    state.meta = {
                        device:      data.meta.device || state.device,
                        width:       data.meta.screen_width  || state.screenW,
                        height:      data.meta.screen_height || state.screenH,
                        orientation: data.meta.orientation || 'portrait',
                    };
                    state.orientation = state.meta.orientation;
                    const btn = $('toggle-orientation');
                    if (btn) {
                        btn.textContent = state.orientation === 'portrait' ? 'Portrait' : 'Landscape';
                        btn.classList.toggle('active', state.orientation === 'portrait');
                    }
                }
                data.elements.forEach(el => engine.addElement(el));
                saveHistory();
                updateElementList();
                updatePropertiesPanel();
                updateStatus();
                alert(t('import_success'));
            } catch (err) {
                alert(t('import_failed') + err.message);
            }
        };
        reader.readAsText(file);
        e.target.value = '';
    });

    /* ═══ 语言切换 ═══ */

    $('lang-switch').addEventListener('click', () => {
        const newLang = currentLang === 'zh' ? 'en' : 'zh';
        applyLanguage(newLang);
        // 重新更新方向按钮文本
        const btn = $('toggle-orientation');
        if (btn) {
            btn.textContent = state.orientation === 'portrait' ? t('portrait') : t('landscape');
        }
    });

    /* ═══ 状态栏 ═══ */

    function updateStatus() {
        const count = engine.elements.length;
        elementCount.textContent = `${count} ${count === 1 ? t('element') : t('elements')}`;
    }

    /* ═══ 初始化 ═══ */

    updateElementList();
    updatePropertiesPanel();
    updateStatus();

    // 初始渲染
    engine.render();

    // 初始化语言
    applyLanguage(currentLang);

    // 暴露引擎到全局，便于调试和自动化测试
    window.uiDesignerEngine = engine;
    window.uiDesignerState  = state;
});
