/**
 * exporter.js — 导出 / 导入 / LLM Prompt 生成
 */

(function (global) {
    'use strict';

    const Exporter = {};

    /* ═══ JSON 导出 ═══ */

    Exporter.toJSON = function(elements, meta) {
        return JSON.stringify({
            meta: {
                device:        meta.device  || 'M5Stick',
                screen_width:  meta.width   || 80,
                screen_height: meta.height  || 160,
                orientation:   meta.orientation || 'portrait',
                version:       '1.0',
                exported_at:   new Date().toISOString(),
            },
            elements: elements.map(el => {
                const out = {
                    id:    el.id,
                    type:  el.subtype || el.type,
                    x:     el.x,
                    y:     el.y,
                    color: el.color,
                    layer: el.layer || 0,
                };
                if (el.w)       out.w       = el.w;
                if (el.h)       out.h       = el.h;
                if (el.r)       out.r       = el.r;
                if (el.fill)    out.fill    = true;
                if (el.x2 !== undefined) { out.x2 = el.x2; out.y2 = el.y2; }
                if (el.text)    out.text    = el.text;
                if (el.fontSize) out.font_size = el.fontSize;
                if (el.iconName) out.icon_name = el.iconName;
                if (el.subtype === 'control') {
                    out.subtype = el.subtype;
                    out.label   = el.label || '';
                }
                return out;
            }),
        }, null, 2);
    };

    /* ═══ JSON 导入 ═══ */

    Exporter.fromJSON = function(jsonStr) {
        const data = JSON.parse(jsonStr);
        const elements = (data.elements || []).map(item => {
            const typeMap = {
                rectangle: 'rectangle',
                'filled-rect': 'filled-rect',
                'round-rect': 'round-rect',
                'filled-round-rect': 'filled-round-rect',
                circle: 'circle',
                'filled-circle': 'filled-circle',
                line: 'line',
                text: 'text',
                icon: 'icon',
                control: 'control',
            };

            const type = typeMap[item.type] || item.type;
            const attrs = {
                id:        item.id,
                x:         item.x,
                y:         item.y,
                color:     item.color || '#FFFFFF',
                layer:     item.layer || 0,
            };
            if (item.w)       attrs.w       = item.w;
            if (item.h)       attrs.h       = item.h;
            if (item.r)       attrs.r       = item.r;
            if (item.fill)    attrs.fill    = true;
            if (item.x2 !== undefined) { attrs.x2 = item.x2; attrs.y2 = item.y2; }
            if (item.text || item.text === '')    attrs.text    = item.text;
            if (item.font_size) attrs.fontSize = item.font_size;
            if (item.icon_name) attrs.iconName = item.icon_name;
            if (item.subtype)   attrs.subtype  = item.subtype;
            if (item.label || item.label === '')  attrs.label   = item.label;

            if (typeof global.UIElements !== 'undefined') {
                return global.UIElements.create(type, attrs);
            }
            return { type, ...attrs };
        });

        return {
            meta:     data.meta || {},
            elements: elements,
        };
    };

    /* ═══ LLM Prompt 生成 ═══ */

    Exporter.toLLMPrompt = function(elements, meta) {
        const w = meta.width  || 80;
        const h = meta.height || 160;
        const dev = meta.device || 'M5Stick';

        let prompt = `# Xerintosh UI Layout — ${dev} (${w}×${h})

You are a helpful embedded UI developer. The user has designed a UI layout on a ${w}×${h} pixel screen using the Xerintosh UI framework.
Your task is to generate embedded C code that reproduces this layout exactly.

## Coordinate System
- Origin (0, 0) is the **top-left** corner.
- X increases to the right, Y increases downward.
- All coordinates are in logical pixels.

## Available HAL Drawing APIs (from hal_display.h)
- void hal_draw_pixel(int16_t x, int16_t y, uint16_t color);
- void hal_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
- void hal_draw_h_line(int16_t x, int16_t y, int16_t len, uint16_t color);
- void hal_draw_v_line(int16_t x, int16_t y, int16_t len, uint16_t color);
- void hal_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
- void hal_draw_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
- void hal_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
- void hal_draw_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
- void hal_draw_circle(int16_t x, int16_t y, int16_t r, uint16_t color);
- void hal_draw_string(int16_t x, int16_t y, const char* str, uint16_t color);
- void hal_draw_utf8(int16_t x, int16_t y, const char* str, uint16_t color);
- void hal_draw_xbitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bitmap);

## Color Constants
- COLOR_BG = 0x0000 (black)
- COLOR_FG = 0xFFFF (white)
- COLOR_ACCENT = 0x07E0 (green)

## UI Item Types (for menus)
- list_item   — plain list entry
- switch_item — toggle switch with ON/OFF state
- slider_item — numeric slider with min/max/step
- button_item — clickable button
- user_item   — fullscreen app entry point

---

## Layout Elements (${elements.length} total)

`;
        // 按 layer 排序
        const sorted = [...elements].sort((a, b) => (a.layer || 0) - (b.layer || 0));

        for (let i = 0; i < sorted.length; i++) {
            const el = sorted[i];
            prompt += `### [${i + 1}] ${el.subtype || el.type} — "${el.id}"\n`;
            prompt += `- Position: x=${el.x}, y=${el.y}`;
            if (el.w) prompt += `, w=${el.w}, h=${el.h}`;
            if (el.r) prompt += `, radius=${el.r}`;
            if (el.x2 !== undefined) prompt += `, to x2=${el.x2}, y2=${el.y2}`;
            prompt += `\n`;
            prompt += `- Color: ${el.color}`;
            if (el.fill) prompt += ` (filled)`;
            prompt += `\n`;
            if (el.text)        prompt += `- Text: "${el.text}" (font size ${el.fontSize || 8}px)\n`;
            if (el.iconName)    prompt += `- Icon: ${el.iconName} (${el.w}x${el.h})\n`;
            if (el.subtype === 'control') {
                prompt += `- Control type: ${el.subtype}\n`;
                prompt += `- Label: "${el.label || ''}"\n`;
            }
            if (el.layer)       prompt += `- Layer: ${el.layer}\n`;
            prompt += `\n`;
        }

        prompt += `## Code Template

\`\`\`c
#include "hal/hal_display.h"
#include "ui/ui_draw_driver.h"
#include "ui/ui_item.h"

// Example initialization
void my_app_init() {
    xerintosh_ui_driver_init();
    hal_display_clear();

    // TODO: Render the layout elements above using HAL APIs
    // hal_draw_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BG);

    hal_display_flush();
}
\`\`\`

Please generate a complete \`draw_layout()\` function that renders all elements in the correct order (by layer), matching the coordinates and colors exactly. Use \`hal_draw_string()\` for ASCII text and \`hal_draw_utf8()\` for CJK characters.
`;

        return prompt;
    };

    /* ═══ 文件下载辅助 ═══ */

    Exporter.downloadFile = function(content, filename, mimeType) {
        const blob = new Blob([content], { type: mimeType || 'text/plain' });
        const url  = URL.createObjectURL(blob);
        const a    = document.createElement('a');
        a.href     = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    };

    /* ═══ 导出到全局 ═══ */

    global.UIExporter = Exporter;

})(window);
