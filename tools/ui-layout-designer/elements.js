/**
 * elements.js — 元素数据模型与渲染
 *
 * 定义所有可在画布上放置的元素类型及其渲染方法。
 * 所有坐标均为逻辑像素（80×160 屏幕空间），渲染时由 canvas.js 负责缩放。
 */

(function (global) {
    'use strict';

    let _idCounter = 0;

    function genId(prefix) {
        return `${prefix}_${++_idCounter}`;
    }

    /* ═══ 元素工厂 ═══ */

    const Elements = {};

    Elements.create = function(type, attrs) {
        attrs = attrs || {};
        const base = {
            id:      attrs.id || genId(type),
            type:    type,
            x:       attrs.x || 0,
            y:       attrs.y || 0,
            w:       attrs.w || 1,
            h:       attrs.h || 1,
            color:   attrs.color || '#FFFFFF',
            layer:   typeof attrs.layer === 'number' ? attrs.layer : 0,
        };

        switch (type) {
            case 'rect':
            case 'rectangle':
            case 'filled-rect':
                return {
                    ...base,
                    fill: type === 'filled-rect',
                    subtype: 'rectangle',
                };
            case 'round-rect':
            case 'filled-round-rect':
                return {
                    ...base,
                    r: attrs.r || 3,
                    fill: type === 'filled-round-rect',
                    subtype: 'round-rect',
                };
            case 'circle':
            case 'filled-circle':
                return {
                    ...base,
                    r: attrs.r || 5,
                    fill: type === 'filled-circle',
                    subtype: 'circle',
                };
            case 'line':
                return {
                    ...base,
                    x2: attrs.x2 || (base.x + 10),
                    y2: attrs.y2 || (base.y + 10),
                    subtype: 'line',
                };
            case 'text':
                return {
                    ...base,
                    text:     attrs.text || '',
                    fontSize: attrs.fontSize || 8,
                    subtype:  'text',
                };
            case 'icon':
                return {
                    ...base,
                    iconName: attrs.iconName || 'wifi',
                    w:        attrs.w || 8,
                    h:        attrs.h || 8,
                    subtype:  'icon',
                };
            case 'control':
                return {
                    ...base,
                    subtype: attrs.subtype || 'list_item',
                    content: attrs.content || '',
                    fontSize: attrs.fontSize || FONT_SIZE,
                    w:       attrs.w || 80,
                    h:       attrs.h || 18,
                };
            default:
                return base;
        }
    };

    /* ═══ 字体配置（与固件 efontCN_12 对齐）═══ */

    /*
     * 字体栈设计思路：
     * 固件使用 efontCN_12（LovyanGFX 内置 12px 位图字体），浏览器无法直接加载该
     * 位图字体。因此采用以下策略：
     *   1. 英文/数字：Share Tech Mono（Google Fonts，等宽，像素风格，与固件接近）
     *   2. 中文：系统自带无衬线字体（PingFang SC / Microsoft YaHei），无法做到
     *      严格的 12px 全角等宽，但 Canvas measureText() 会实际测量每个字符宽度，
     *      因此布局计算仍然准确。
     *   3. 不加载外部 CJK 字体文件（通常 5-10MB），避免拖慢纯前端设计工具。
     */
    const FONT_FAMILY = '"Share Tech Mono", "PingFang SC", "Microsoft YaHei", "Noto Sans SC", monospace';
    const FONT_SIZE = 12;  /* 固件 hal_get_font_height() 返回值 */

    /* 隐藏 canvas 用于精确文字度量 */
    const _measureCanvas = document.createElement('canvas');
    const _measureCtx = _measureCanvas.getContext('2d');

    function measureTextWidth(text, fontSize) {
        fontSize = fontSize || FONT_SIZE;
        _measureCtx.font = `${fontSize}px ${FONT_FAMILY}`;
        return Math.ceil(_measureCtx.measureText(String(text)).width);
    }

    /* ═══ 渲染器 — 所有函数接收 (ctx, el, scale)，在已缩放的 ctx 上绘制 ═══ */

    function hexToRgb565(hex) {
        const r = parseInt(hex.slice(1, 3), 16);
        const g = parseInt(hex.slice(3, 5), 16);
        const b = parseInt(hex.slice(5, 7), 16);
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }

    function setColor(ctx, hex) {
        ctx.strokeStyle = hex;
        ctx.fillStyle   = hex;
    }

    const Renderers = {};

    Renderers.rectangle = function(ctx, el) {
        setColor(ctx, el.color);
        if (el.fill) {
            ctx.fillRect(el.x, el.y, el.w, el.h);
        } else {
            ctx.strokeRect(el.x + 0.5, el.y + 0.5, el.w - 1, el.h - 1);
        }
    };

    /**
     * 绘制圆角矩形（兼容旧浏览器，不使用 ctx.roundRect）
     */
    function drawRoundRectPath(ctx, x, y, w, h, r) {
        const radius = Math.min(r, w / 2, h / 2);
        ctx.beginPath();
        ctx.moveTo(x + radius, y);
        ctx.lineTo(x + w - radius, y);
        ctx.arcTo(x + w, y, x + w, y + radius, radius);
        ctx.lineTo(x + w, y + h - radius);
        ctx.arcTo(x + w, y + h, x + w - radius, y + h, radius);
        ctx.lineTo(x + radius, y + h);
        ctx.arcTo(x, y + h, x, y + h - radius, radius);
        ctx.lineTo(x, y + radius);
        ctx.arcTo(x, y, x + radius, y, radius);
        ctx.closePath();
    }

    Renderers['round-rect'] = function(ctx, el) {
        setColor(ctx, el.color);
        const r = Math.min(el.r || 3, el.w / 2, el.h / 2);
        drawRoundRectPath(ctx, el.x + 0.5, el.y + 0.5, el.w - 1, el.h - 1, r);
        if (el.fill) {
            ctx.fill();
        } else {
            ctx.stroke();
        }
    };

    Renderers.circle = function(ctx, el) {
        setColor(ctx, el.color);
        ctx.beginPath();
        ctx.arc(el.x + el.r, el.y + el.r, el.r, 0, Math.PI * 2);
        if (el.fill) {
            ctx.fill();
        } else {
            ctx.stroke();
        }
    };

    Renderers.line = function(ctx, el) {
        setColor(ctx, el.color);
        ctx.beginPath();
        ctx.moveTo(el.x + 0.5, el.y + 0.5);
        ctx.lineTo(el.x2 + 0.5, el.y2 + 0.5);
        ctx.stroke();
    };

    // 简单像素字体宽度估算：每个字符约 0.6 * fontSize
    function measureTextWidth(text, fontSize) {
        let w = 0;
        for (const ch of String(text)) {
            const code = ch.charCodeAt(0);
            // CJK 字符占全宽
            if (code >= 0x4e00 && code <= 0x9fff) {
                w += fontSize;
            } else {
                w += Math.max(1, Math.floor(fontSize * 0.6));
            }
        }
        return w;
    }

    Renderers.text = function(ctx, el) {
        if (!el.text) return;
        setColor(ctx, el.color);
        const fs = el.fontSize || FONT_SIZE;
        ctx.font = `${fs}px ${FONT_FAMILY}`;
        ctx.textBaseline = 'top';
        ctx.fillText(el.text, el.x, el.y);
        // 更新元素的 w/h 以反映实际文本尺寸（用于选择和边界框）
        el.w = measureTextWidth(el.text, fs);
        el.h = fs;
    };

    // 图标位图数据（8×8，1=前景色）
    // 固件标准图标（与 ui_item.h 中的 xerintosh_list_item_icon_t 对齐）
    const ICON_BITMAPS = {
        // ─── 固件标准图标 ───
        default_icon: {
            w: 8, h: 8,
            data: [
                0b00000000, 0b00000000, 0b00000000, 0b00000000,
                0b00000000, 0b00000000, 0b00000000, 0b00000000,
            ]
        },
        list_icon: {
            w: 8, h: 8,
            data: [
                0b00000000,
                0b00111100,
                0b00000000,
                0b00111110,
                0b00000000,
                0b00111000,
                0b00000000,
                0b00000000,
            ]
        },
        switch_icon: {
            w: 8, h: 8,
            data: [
                0b00011000,
                0b00100100,
                0b01000010,
                0b01011010,
                0b01000010,
                0b00100100,
                0b00011000,
                0b00000000,
            ]
        },
        plus_icon: {
            w: 8, h: 8,
            data: [
                0b00011000,
                0b00100100,
                0b01011010,
                0b01000010,
                0b01011010,
                0b00100100,
                0b00011000,
                0b00000000,
            ]
        },
        user_icon: {
            w: 8, h: 8,
            data: [
                0b00011000,
                0b00111100,
                0b00111100,
                0b00011000,
                0b00111100,
                0b01000010,
                0b01000010,
                0b00000000,
            ]
        },
        slider_icon: {
            w: 8, h: 8,
            data: [
                0b00100100,
                0b00100100,
                0b01100110,
                0b00100100,
                0b00100100,
                0b01100110,
                0b00100100,
                0b00000000,
            ]
        },
        flag_icon: {
            w: 8, h: 8,
            data: [
                0b00000100,
                0b00000100,
                0b00111100,
                0b00111100,
                0b00000100,
                0b00000100,
                0b00000100,
                0b00000000,
            ]
        },
        power_icon: {
            w: 8, h: 8,
            data: [
                0b00011000,
                0b00100100,
                0b00100100,
                0b00100100,
                0b00100100,
                0b00100100,
                0b00011000,
                0b00010000,
            ]
        },
        custom_icon: {
            w: 8, h: 8,
            data: [
                0b00111100,
                0b01000010,
                0b00000100,
                0b00001000,
                0b00010000,
                0b00000000,
                0b00010000,
                0b00000000,
            ]
        },
        // ─── 扩展图标（精美） ───
        wifi: {
            w: 8, h: 8,
            data: [
                0b00000000,
                0b00111100,
                0b01000010,
                0b10011001,
                0b00100100,
                0b01000010,
                0b00011000,
                0b00000000,
            ]
        },
        bluetooth: {
            w: 8, h: 8,
            data: [
                0b00010000,
                0b00101000,
                0b00010000,
                0b00101000,
                0b00010000,
                0b00101000,
                0b00010000,
                0b00000000,
            ]
        },
        battery: {
            w: 8, h: 8,
            data: [
                0b01111110,
                0b01000010,
                0b01111110,
                0b01111110,
                0b01111110,
                0b01111110,
                0b01111110,
                0b00000000,
            ]
        },
        heart: {
            w: 8, h: 8,
            data: [
                0b00000000,
                0b01100110,
                0b11111111,
                0b11111111,
                0b01111110,
                0b00111100,
                0b00011000,
                0b00000000,
            ]
        },
        settings: {
            w: 8, h: 8,
            data: [
                0b00010000,
                0b00111000,
                0b00010000,
                0b01101100,
                0b00010000,
                0b00111000,
                0b00010000,
                0b00000000,
            ]
        },
    };

    Renderers.icon = function(ctx, el) {
        const bmp = ICON_BITMAPS[el.iconName] || ICON_BITMAPS.wifi;
        const cw = Math.floor(el.w / bmp.w);
        const ch = Math.floor(el.h / bmp.h);
        const cellSize = Math.min(cw, ch);
        const ox = el.x + Math.floor((el.w - bmp.w * cellSize) / 2);
        const oy = el.y + Math.floor((el.h - bmp.h * cellSize) / 2);

        setColor(ctx, el.color);
        for (let row = 0; row < bmp.h; row++) {
            const byte = bmp.data[row];
            for (let col = 0; col < bmp.w; col++) {
                if ((byte >> (7 - col)) & 1) {
                    ctx.fillRect(ox + col * cellSize, oy + row * cellSize, cellSize, cellSize);
                }
            }
        }
    };

    /* ── 与固件对齐的布局常量 ── */
    const LIST_ITEM_LEFT_MARGIN = 4;   /* ui_item.h */
    const LIST_ITEM_RIGHT_MARGIN = 20; /* ui_item.h */
    const LIST_FONT_TOP_MARGIN = 6;    /* ui_item.h */

    /**
     * 像素级图标绘制（与固件 xerintosh_draw_list_icon 对齐）
     * @param x 图标左上角 x（图标以 y 为中心）
     * @param y 图标中心 y
     */
    function drawListIcon(ctx, icon, x, y, color) {
        setColor(ctx, color);
        switch (icon) {
            case 'list_icon':
                ctx.fillRect(2 + x, y - 2, 4, 1);
                ctx.fillRect(2 + x, y,     5, 1);
                ctx.fillRect(2 + x, y + 2, 3, 1);
                break;
            case 'switch_icon':
                ctx.beginPath(); ctx.arc(4 + x, y + 1, 3, 0, Math.PI * 2); ctx.stroke();
                ctx.fillRect(4 + x, y, 1, 3);
                break;
            case 'plus_icon':
                ctx.beginPath(); ctx.arc(4 + x, y + 1, 3, 0, Math.PI * 2); ctx.stroke();
                ctx.fillRect(4 + x, y, 1, 3);
                ctx.fillRect(3 + x, y + 1, 3, 1);
                break;
            case 'slider_icon':
                ctx.fillRect(3 + x, y - 1, 1, 5);
                ctx.fillRect(6 + x, y - 1, 1, 5);
                ctx.fillRect(2 + x, y - 2, 3, 3);
                ctx.fillRect(5 + x, y + 2, 3, 3);
                break;
            case 'user_icon':
                ctx.font = `${FONT_SIZE}px ${FONT_FAMILY}`;
                ctx.textBaseline = 'middle';
                ctx.fillText('-', 2 + x, y + FONT_SIZE / 2);
                break;
            case 'flag_icon':
                ctx.fillRect(6 + x, y - 1, 1, 5);
                ctx.fillRect(3 + x, y - 2, 4, 3);
                break;
            case 'power_icon':
                ctx.beginPath(); ctx.arc(4 + x, y + 1, 3, 0, Math.PI * 2); ctx.stroke();
                ctx.fillRect(4 + x, y - 2, 1, 3);
                // 覆盖顶端像素（与固件一致）
                ctx.fillStyle = '#000';
                ctx.fillRect(x + 3, y - 2, 1, 1);
                ctx.fillRect(x + 5, y - 2, 1, 1);
                break;
            default:
                break;
        }
    }

    Renderers.control = function(ctx, el) {
        const subtype = el.subtype || 'list_item';
        const content = el.content || '';
        const fontSize = el.fontSize || FONT_SIZE;
        const yCenter = el.y + Math.floor(el.h / 2);

        /* 1. 左侧图标（与固件像素级对齐） */
        const iconMap = {
            list_item:   'list_icon',
            switch_item: 'switch_icon',
            slider_item: 'slider_icon',
            button_item: 'plus_icon',
            user_item:   'user_icon',
        };
        drawListIcon(ctx, iconMap[subtype] || 'list_icon',
                     el.x + LIST_ITEM_LEFT_MARGIN, yCenter, el.color);

        /* 2. 文字（content） */
        if (content) {
            setColor(ctx, el.color);
            ctx.font = `${fontSize}px ${FONT_FAMILY}`;
            ctx.textBaseline = 'middle';

            const hasRightControl = (subtype === 'switch_item' || subtype === 'slider_item');
            const hasRightArrow   = (subtype === 'button_item' || subtype === 'user_item');
            const rightMargin = hasRightControl || hasRightArrow ? LIST_ITEM_RIGHT_MARGIN : 4;
            let availWidth = el.w - LIST_ITEM_LEFT_MARGIN - 10 - rightMargin;
            if (subtype === 'switch_item') availWidth -= 11;
            else if (subtype === 'slider_item') availWidth -= 11;

            const textX = el.x + LIST_ITEM_LEFT_MARGIN + 10;

            ctx.save();
            ctx.beginPath();
            ctx.rect(textX, el.y, availWidth, el.h);
            ctx.clip();
            ctx.fillText(content, textX, yCenter + 1);
            ctx.restore();
        }

        /* 3. 右侧控件（与固件像素级对齐） */
        const rightX = el.x + el.w - LIST_ITEM_RIGHT_MARGIN;
        if (subtype === 'switch_item') {
            // 开关外框 11×7（固件 draw_list_item_switch）
            setColor(ctx, el.color);
            ctx.strokeRect(rightX - 7, yCenter - 2, 11, 7);
            // 开启态：方块靠右
            ctx.fillRect(rightX - 1, yCenter,     3, 3);
            ctx.fillRect(rightX - 4, yCenter + 1, 1, 1);
        } else if (subtype === 'slider_item') {
            // 数值文本 + 反色圆角矩形背景（固件 xerintosh_draw_slider_overlays）
            const valueStr = String(el.value !== undefined ? el.value : 50);
            const valueWidth = measureTextWidth(valueStr, fontSize);
            const xValue = rightX - valueWidth + 2;
            // 反色背景框
            ctx.fillStyle = '#000';
            ctx.fillRect(xValue, yCenter - 2, valueWidth + 4, fontSize - 2);
            // 数值文字
            setColor(ctx, el.color);
            ctx.font = `${fontSize}px ${FONT_FAMILY}`;
            ctx.textBaseline = 'middle';
            ctx.fillText(valueStr, xValue + 2, yCenter + fontSize / 2);
        } else if (subtype === 'button_item' || subtype === 'user_item') {
            // button_item / user_item：右侧箭头
            setColor(ctx, el.color);
            const ay = yCenter + 1;
            ctx.beginPath();
            ctx.moveTo(rightX + 2, ay - 3);
            ctx.lineTo(rightX + 7, ay);
            ctx.lineTo(rightX + 2, ay + 3);
            ctx.closePath();
            ctx.fill();
        }
        // list_item: 不绘制任何右侧控件
    };

    /* ═══ 公共渲染入口 ═══ */

    Elements.render = function(ctx, el) {
        // control 类型的 renderer 统一注册在 'control' key 下，
        // 但其 subtype 为 'list_item'/'switch_item' 等，需特殊处理
        const subtype = el.type === 'control' ? 'control' : (el.subtype || el.type);
        const renderer = Renderers[subtype];
        if (renderer) {
            renderer(ctx, el);
        }
    };

    /* ═══ 边界框检测 ═══ */

    Elements.hitTest = function(el, x, y) {
        const t = 2; // 命中容差（逻辑像素）
        switch (el.subtype || el.type) {
            case 'rectangle':
            case 'round-rect':
            case 'filled-rect':
            case 'filled-round-rect':
                return x >= el.x - t && x <= el.x + el.w + t &&
                       y >= el.y - t && y <= el.y + el.h + t;
            case 'circle':
            case 'filled-circle': {
                const cx = el.x + el.r;
                const cy = el.y + el.r;
                const dx = x - cx;
                const dy = y - cy;
                return dx * dx + dy * dy <= (el.r + t) * (el.r + t);
            }
            case 'line': {
                // 点到线段的距离
                const A = x - el.x;
                const B = y - el.y;
                const C = el.x2 - el.x;
                const D = el.y2 - el.y;
                const dot = A * C + B * D;
                const lenSq = C * C + D * D;
                let param = -1;
                if (lenSq !== 0) param = dot / lenSq;
                let xx, yy;
                if (param < 0) { xx = el.x; yy = el.y; }
                else if (param > 1) { xx = el.x2; yy = el.y2; }
                else { xx = el.x + param * C; yy = el.y + param * D; }
                const dx = x - xx;
                const dy = y - yy;
                return dx * dx + dy * dy <= t * t;
            }
            case 'text':
            case 'icon':
            case 'control':
            case 'list_item':
            case 'switch_item':
            case 'slider_item':
            case 'button_item':
            case 'user_item':
                return x >= el.x - t && x <= el.x + el.w + t &&
                       y >= el.y - t && y <= el.y + el.h + t;
            default:
                return false;
        }
    };

    /* ═══ 调整大小手柄 ═══ */

    Elements.getHandles = function(el) {
        const type = el.subtype || el.type;
        if (type === 'line') {
            return [
                { x: el.x,  y: el.y,  name: 'start' },
                { x: el.x2, y: el.y2, name: 'end' },
            ];
        }
        if (type === 'circle' || type === 'filled-circle') {
            return [
                { x: el.x, y: el.y, name: 'top-left' },
                { x: el.x + el.r * 2, y: el.y, name: 'top-right' },
                { x: el.x, y: el.y + el.r * 2, name: 'bottom-left' },
                { x: el.x + el.r * 2, y: el.y + el.r * 2, name: 'bottom-right' },
            ];
        }
        // 矩形类通用 4 角
        return [
            { x: el.x,      y: el.y,      name: 'top-left' },
            { x: el.x + el.w, y: el.y,      name: 'top-right' },
            { x: el.x,      y: el.y + el.h, name: 'bottom-left' },
            { x: el.x + el.w, y: el.y + el.h, name: 'bottom-right' },
        ];
    };

    Elements.hitHandle = function(el, x, y) {
        const handles = Elements.getHandles(el);
        for (const h of handles) {
            if (Math.abs(x - h.x) <= 2 && Math.abs(y - h.y) <= 2) {
                return h.name;
            }
        }
        return null;
    };

    /* ═══ 克隆 ═══ */

    Elements.clone = function(el) {
        const copy = JSON.parse(JSON.stringify(el));
        copy.id = genId(el.type);
        copy.x += 5;
        copy.y += 5;
        return copy;
    };

    /* ═══ 属性描述 ═══ */

    Elements.getPropFields = function(el) {
        const common = ['id', 'x', 'y', 'color', 'layer'];
        // control 元素使用 type='control'，subtype 为 'list_item'/'switch_item' 等
        if (el.type === 'control') {
            return [...common, 'w', 'h', 'content', 'fontSize', 'subtype'];
        }
        switch (el.subtype || el.type) {
            case 'rectangle':
            case 'filled-rect':
                return [...common, 'w', 'h'];
            case 'round-rect':
            case 'filled-round-rect':
                return [...common, 'w', 'h', 'r'];
            case 'circle':
            case 'filled-circle':
                return [...common, 'r'];
            case 'line':
                return [...common, 'x2', 'y2'];
            case 'text':
                return [...common, 'text', 'fontSize'];
            case 'icon':
                return [...common, 'w', 'h', 'iconName'];
            default:
                return common;
        }
    };

    Elements.getDisplayName = function(el) {
        if (el.type === 'control') {
            return el.content || el.subtype;
        }
        if (el.subtype === 'text') {
            const t = el.text || '';
            return t.length > 10 ? t.slice(0, 10) + '…' : t;
        }
        if (el.subtype === 'icon') {
            return el.iconName;
        }
        return el.subtype || el.type;
    };

    /* ═══ 导出到全局 ═══ */

    global.UIElements = Elements;
    global.ICON_BITMAPS = ICON_BITMAPS;

})(window);
