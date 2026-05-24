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
                    fontSize: attrs.fontSize || 8,   /* 固件默认 small 字体 */
                    w:       attrs.w || 80,
                    h:       attrs.h || 18,          /* LIST_ITEM_SPACING = 18，固定不变 */
                };
            case 'list':
                return {
                    ...base,
                    subtype: 'list',
                    w:       attrs.w || 80,
                    h:       attrs.h || 160,
                    children: attrs.children || [],
                    selectedIndex: typeof attrs.selectedIndex === 'number' ? attrs.selectedIndex : 0,
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

    /* ═══ 布局引擎（与固件 ui_item.c / ui_core.c 对齐）═══ */

    const UILayoutEngine = {
        FONT_HEIGHT: 12,
        LIST_ITEM_SPACING: 18,
        LIST_FONT_TOP_MARGIN: 6,
        LIST_ITEM_LEFT_MARGIN: 4,
        LIST_ITEM_RIGHT_MARGIN: 20,
        SCREEN_WIDTH: 80,
        SCREEN_HEIGHT: 160,

        /**
         * 自动布局 list 内所有子项的 y 坐标
         * 与固件 xerintosh_push_item_to_list 计算逻辑对齐
         */
        layoutListChildren(list) {
            const firstY = list.y + this.FONT_HEIGHT + this.LIST_FONT_TOP_MARGIN - 1;
            for (let i = 0; i < list.children.length; i++) {
                const child = list.children[i];
                child.x = list.x;
                child.y = firstY + i * this.LIST_ITEM_SPACING;
                child.w = list.w;
                child.h = this.LIST_ITEM_SPACING;
            }
        },

        /**
         * 计算选择器高亮框的位置与尺寸
         * 与固件 xerintosh_refresh_selector_position 对齐
         */
        computeSelectorRect(list) {
            const fontHeight = this.FONT_HEIGHT;
            const selectedChild = list.children[list.selectedIndex];
            if (!selectedChild) {
                return { x: list.x + this.LIST_ITEM_LEFT_MARGIN, y: list.y, w: list.w, h: fontHeight + 4 };
            }

            const y = selectedChild.y - fontHeight + 1;
            const h = fontHeight + 4;
            let w;
            const subtype = selectedChild.subtype;
            if (subtype === 'switch_item' || subtype === 'slider_item') {
                w = this.SCREEN_WIDTH - 18; // 62
            } else {
                const textWidth = measureTextWidth(selectedChild.content || '', selectedChild.fontSize || fontHeight);
                w = textWidth + 12;
            }
            return { x: list.x + this.LIST_ITEM_LEFT_MARGIN, y: y, w: w, h: h };
        },

        /**
         * 计算相机偏移，确保选择器始终在可视区域内
         * 与固件 xerintosh_refresh_camera_position 对齐
         */
        computeCameraOffset(selectorY, screenHeight, selectorHeight) {
            // 向下越界：selector 底部超出屏幕
            if (selectorY + selectorHeight > screenHeight) {
                return screenHeight - selectorY - selectorHeight;
            }
            // 向上越界：selector 顶部在屏幕上方
            if (selectorY < 0) {
                return -selectorY + this.LIST_FONT_TOP_MARGIN;
            }
            return 0;
        },
    };

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
     * 像素级图标绘制（与固件 xerintosh_draw_list_icon 逐像素对齐）
     *
     * 固件语义：
     *   - x 为图标左上角 x
     *   - y 为图标中心 y（文字基线锚点 yBaseline 减去字体高度的一半）
     *
     * 设计器对齐方式：
     *   所有 fillRect / arc / stroke 的坐标均与固件 HAL 调用一一对应。
     */
    function drawListIcon(ctx, icon, x, y, color) {
        setColor(ctx, color);
        switch (icon) {
            case 'list_icon':
                // 固件：三条横线
                ctx.fillRect(2 + x, y - 2, 4, 1);
                ctx.fillRect(2 + x, y,     5, 1);
                ctx.fillRect(2 + x, y + 2, 3, 1);
                break;
            case 'switch_icon':
                // 固件：圆(4+x, y+1, r=3) + 竖线(4+x, y, 长3)
                ctx.beginPath();
                ctx.arc(4 + x, y + 1, 3, 0, Math.PI * 2);
                ctx.stroke();
                ctx.fillRect(4 + x, y, 1, 3);
                break;
            case 'plus_icon':
                // 固件：圆(4+x, y+1, r=3) + 竖线(4+x, y, 长3) + 横线(3+x, y+1, 长3)
                ctx.beginPath();
                ctx.arc(4 + x, y + 1, 3, 0, Math.PI * 2);
                ctx.stroke();
                ctx.fillRect(4 + x, y, 1, 3);
                ctx.fillRect(3 + x, y + 1, 3, 1);
                break;
            case 'slider_icon':
                // 固件：两根竖条 + 两个方块
                ctx.fillRect(3 + x, y - 1, 1, 5);
                ctx.fillRect(6 + x, y - 1, 1, 5);
                ctx.fillRect(2 + x, y - 2, 3, 3);
                ctx.fillRect(5 + x, y + 2, 3, 3);
                break;
            case 'user_icon':
                // 固件：hal_draw_string(2+x, y + hal_get_font_height()/2, "-", ...)
                // y 参数为文字基线，即图标中心 + fontSize/2
                ctx.font = `${FONT_SIZE}px ${FONT_FAMILY}`;
                ctx.textBaseline = 'alphabetic';
                ctx.fillText('-', 2 + x, y + FONT_SIZE / 2);
                break;
            case 'flag_icon':
                // 固件：竖线(6+x, y-1, 长5) + fillRect(3+x, y-2, 4, 3)
                ctx.fillRect(6 + x, y - 1, 1, 5);
                ctx.fillRect(3 + x, y - 2, 4, 3);
                break;
            case 'power_icon':
                // 固件：圆(4+x, y+1, r=3) + 竖线(4+x, y-2, 长3)
                // 然后覆盖 (x+3, y-2) 和 (x+5, y-2) 为背景色
                ctx.beginPath();
                ctx.arc(4 + x, y + 1, 3, 0, Math.PI * 2);
                ctx.stroke();
                ctx.fillRect(4 + x, y - 2, 1, 3);
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

        /* ═══ 与固件对齐的垂直坐标系 ═══
         *
         * 固件中列表项的 y_list_item 是文字基线锚点：
         *   - 第一项 y_list_item = hal_get_font_height() + LIST_FONT_TOP_MARGIN - 1
         *   - 图标中心 = y_list_item - hal_get_font_height()/2
         *   - 文字基线 = y_list_item
         *
         * 设计器 control 元素是一个矩形 (el.x, el.y, el.w, el.h)。
         * 为保持视觉一致，定义：
         *   - yBaseline  = el.y + el.h - 1   // 文字基线，对应固件 y_list_item
         *   - yIconCenter = yBaseline - Math.floor(fontSize / 2)  // 图标中心
         */
        const yBaseline   = el.y + el.h - 1;
        const yIconCenter = yBaseline - Math.floor(fontSize / 2);

        /* 1. 左侧图标（与固件像素级对齐） */
        const iconMap = {
            list_item:   'list_icon',
            switch_item: 'switch_icon',
            slider_item: 'slider_icon',
            button_item: 'plus_icon',
            user_item:   'user_icon',
        };
        drawListIcon(ctx, iconMap[subtype] || 'list_icon',
                     el.x + LIST_ITEM_LEFT_MARGIN, yIconCenter, el.color);

        /* 2. 文字（content） */
        if (content) {
            setColor(ctx, el.color);
            ctx.font = `${fontSize}px ${FONT_FAMILY}`;
            ctx.textBaseline = 'alphabetic';

            /* 固件中仅 switch_item / slider_item 需要右侧控件空间（LIST_ITEM_RIGHT_MARGIN=20）
               list_item / button_item / user_item 的右侧 margin 只有 4 像素 */
            const hasRightControl = (subtype === 'switch_item' || subtype === 'slider_item');
            const rightMargin = hasRightControl ? LIST_ITEM_RIGHT_MARGIN : 4;
            let availWidth = el.w - LIST_ITEM_LEFT_MARGIN - 10 - rightMargin;
            if (subtype === 'switch_item') availWidth -= 11;
            else if (subtype === 'slider_item') availWidth -= 11;

            const textX = el.x + LIST_ITEM_LEFT_MARGIN + 10;

            /* 裁剪区域与固件 hal_set_clip_rect 对齐：
               固件：clip_y = _y_list_item - hal_get_font_height()/2 - 2
                     clip_h = hal_get_font_height() + 4
               设计器：clip_y = yIconCenter - Math.floor(fontSize/2) - 2
                       clip_h = fontSize + 4                       */
            const clipY = yIconCenter - Math.floor(fontSize / 2) - 2;
            const clipH = fontSize + 4;

            ctx.save();
            ctx.beginPath();
            ctx.rect(textX, clipY, availWidth, clipH);
            ctx.clip();
            // 固件：hal_draw_utf8(x, y_list_item + hal_get_font_height()/2, content, ...)
            // 其中 y_list_item + hal_get_font_height()/2 正是 yBaseline（因为 yBaseline = y_list_item）
            ctx.fillText(content, textX, yBaseline);
            ctx.restore();
        }

        /* 3. 右侧控件（与固件像素级对齐） */
        const rightX = el.x + el.w - LIST_ITEM_RIGHT_MARGIN;
        if (subtype === 'switch_item') {
            // 固件 draw_list_item_switch：
            //   hal_draw_rect(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 7, _y - 2, 11, 7)
            //   _y = y_list_item - hal_get_font_height()/2 = yIconCenter
            setColor(ctx, el.color);
            ctx.strokeRect(rightX - 7, yIconCenter - 2, 11, 7);
            // 开启态：方块靠右
            ctx.fillRect(rightX - 1, yIconCenter,     3, 3);
            ctx.fillRect(rightX - 4, yIconCenter + 1, 1, 1);
            // 注意：设计器只显示一种状态（默认 ON），实际固件会根据 *value 切换
        } else if (subtype === 'slider_item') {
            // 固件在未确认态下直接显示数值；确认态有反色背景。
            // 设计器默认显示数值（未确认态）
            const valueStr = String(el.value !== undefined ? el.value : 50);
            const valueWidth = measureTextWidth(valueStr, fontSize);
            const xValue = rightX - valueWidth + 2;

            // 固件未确认态：hal_draw_string(_x_value + 2, _y + hal_get_font_height()/2, ...)
            // _y + hal_get_font_height()/2 = yIconCenter + fontSize/2 = yBaseline
            setColor(ctx, el.color);
            ctx.font = `${fontSize}px ${FONT_FAMILY}`;
            ctx.textBaseline = 'alphabetic';
            ctx.fillText(valueStr, xValue + 2, yBaseline);
        }
        // list_item / button_item / user_item: 不绘制任何右侧控件
        // （固件中 button_item / user_item 没有右侧箭头，进入提示由选择器虚线装饰承担）
    };

    /* ═══ List 容器渲染（与固件 ui_drawer.c 对齐）═══ */

    Renderers.list = function(ctx, el) {
        const engine = UILayoutEngine;
        engine.layoutListChildren(el);

        /* ── 1. 列表外观（与 xerintosh_draw_list_appearance 像素级对齐）── */
        setColor(ctx, el.color);

        // 顶部装饰横线
        ctx.fillRect(el.x, el.y + 1, 66, 1);
        ctx.fillRect(el.x, el.y, 67, 1);

        // 顶部右侧装饰像素
        const decoPixels = [
            [67, 1], [69, 1], [71, 1], [73, 1], [75, 1], [77, 1], [79, 1], [81, 1], [83, 1], [85, 1], [87, 1], [89, 1], [91, 1], [93, 1], [95, 1], [97, 1], [99, 1],
            [68, 0], [70, 0], [72, 0], [74, 0], [76, 0], [78, 0], [80, 0], [82, 0], [84, 0], [86, 0], [88, 0], [90, 0], [92, 0], [94, 0], [96, 0], [98, 0], [100, 0],
            [102, 1], [105, 1], [108, 1], [111, 1],
            [103, 0], [106, 0], [109, 0], [112, 0],
            [115, 1], [120, 1], [125, 1],
            [116, 0], [121, 0], [126, 0],
        ];
        for (const [dx, dy] of decoPixels) {
            if (dx < el.w) ctx.fillRect(el.x + dx, el.y + dy, 1, 1);
        }

        // 右侧边框竖线
        ctx.fillRect(el.x + el.w - 5, el.y, 1, el.h);
        ctx.fillRect(el.x + el.w - 1, el.y, 1, el.h);

        // 滚动条
        const childCount = el.children.length;
        if (childCount > 0) {
            const lengthEachPart = Math.ceil((el.h - 10) / childCount);
            const scrollY = 5 + el.selectedIndex * lengthEachPart;
            ctx.fillRect(el.x + el.w - 4, el.y + scrollY, 3, lengthEachPart);

            // 滚动条内部高光线（背景色覆盖）
            ctx.fillStyle = '#000000';
            ctx.fillRect(el.x + el.w - 4, el.y + lengthEachPart + scrollY, 3, 1);
            if (lengthEachPart >= 9) {
                ctx.fillRect(el.x + el.w - 4, el.y + Math.floor(lengthEachPart - 2 + scrollY), 3, 1);
                ctx.fillRect(el.x + el.w - 4, el.y + Math.floor(lengthEachPart + 2 + scrollY), 3, 1);
            }
            setColor(ctx, el.color);
        }

        // 滚动条上下端点
        ctx.fillRect(el.x + el.w - 4, el.y, 3, 4);
        ctx.fillRect(el.x + el.w - 4, el.y + el.h - 4, 3, 4);
        ctx.fillStyle = '#000000';
        ctx.fillRect(el.x + el.w - 4, el.y + 2, 3, 1);
        ctx.fillRect(el.x + el.w - 3, el.y + 1, 1, 1);
        ctx.fillRect(el.x + el.w - 4, el.y + el.h - 3, 3, 1);
        ctx.fillRect(el.x + el.w - 3, el.y + el.h - 2, 1, 1);
        setColor(ctx, el.color);

        /* ── 2. 计算相机偏移 ── */
        const selRect = engine.computeSelectorRect(el);
        const cameraY = engine.computeCameraOffset(selRect.y - el.y, el.h, selRect.h);

        /* ── 3. 绘制子元素（带相机偏移）── */
        ctx.save();
        for (const child of el.children) {
            const origY = child.y;
            child.y = origY + cameraY;
            Elements.render(ctx, child);
            child.y = origY;
        }
        ctx.restore();

        /* ── 4. 绘制选择器高亮框（与固件 xerintosh_draw_selector 对齐）── */
        const selX = selRect.x;
        const selY = selRect.y + cameraY;
        const selW = selRect.w;
        const selH = selRect.h;

        // XOR 反色矩形效果：用黄色虚线矩形模拟
        ctx.strokeStyle = '#FFD700';
        ctx.lineWidth = 1;
        ctx.setLineDash([2, 2]);
        ctx.strokeRect(selX, selY, selW, selH);
        ctx.setLineDash([]);

        // 右侧虚线装饰
        setColor(ctx, el.color);
        for (let i = selX + selW; i <= selX + selW + 7; i += 2) {
            for (let j = selY; j <= selY + selH - 1; j++) {
                if (Math.floor(j) % 2 === 0) {
                    ctx.fillRect(i + 1, j, 1, 1);
                } else {
                    ctx.fillRect(i, j, 1, 1);
                }
            }
        }
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
            case 'list':
                return x >= el.x - t && x <= el.x + el.w + t &&
                       y >= el.y - t && y <= el.y + el.h + t;
            default:
                return false;
        }
    };

    /* ═══ 调整大小手柄 ═══ */

    Elements.getHandles = function(el) {
        /* 系统控件和 list 大小固定，不提供 resize 手柄 */
        if (el.type === 'control' || el.type === 'list') return [];

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
            case 'list':
                return [...common, 'w', 'h', 'selectedIndex'];
            default:
                return common;
        }
    };

    Elements.getDisplayName = function(el) {
        if (el.type === 'list') {
            return `List (${el.children.length})`;
        }
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
    global.UILayoutEngine = UILayoutEngine;

})(window);
