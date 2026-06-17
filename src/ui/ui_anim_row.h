/**
 * @file   ui_anim_row.h
 * @brief  公共行列表动画工具头文件
 * @details 定义 xerintosh_anim_row_t（单行动画状态）和
 *          xerintosh_anim_row_list_t（列表动画上下文），
 *          供所有行列表 App（任务管理器、串口监视器等）复用。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef UI_ANIM_ROW_H
#define UI_ANIM_ROW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══ 常量 ═══ */

#define ANIM_ROW_MAX 10  /* 最大动画行数 */

/* ═══ 数据结构 ═══ */

/**
 * @brief 单行动画状态
 */
typedef struct {
    float y;         /* 当前动画 Y */
    float y_trg;     /* 目标 Y */
    float w;         /* 当前动画宽度（高亮框用） */
    float w_trg;     /* 目标宽度 */
    float v_y;       /* Y 轴弹簧速度 */
    float v_w;       /* 宽度弹簧速度 */
} xerintosh_anim_row_t;

/**
 * @brief 行列表动画上下文
 */
typedef struct {
    xerintosh_anim_row_t rows[ANIM_ROW_MAX];  /* 各行动画状态 */
    xerintosh_anim_row_t highlight;           /* 高亮框动画状态 */
    float   scroll_offset;                    /* 当前滚动偏移 */
    float   scroll_offset_trg;                /* 目标滚动偏移 */
    float   v_scroll_offset;                  /* 滚动偏移弹簧速度 */
    int     visible_count;                    /* 可见行数 */
    int16_t row_height;                       /* 行高 */
    int16_t list_top;                         /* 列表顶部 Y */
} xerintosh_anim_row_list_t;

/* ═══ API ═══ */

/**
 * @brief 初始化行列表动画上下文
 * @param list           动画上下文指针
 * @param visible_count  可见行数
 * @param row_height     行高（含间距）
 * @param list_top       列表顶部 Y 坐标
 * @note  所有行初始 Y 设为 SCREEN_HEIGHT 以实现入场动画效果
 */
void xerintosh_anim_row_list_init(xerintosh_anim_row_list_t *list,
                                   int visible_count, int16_t row_height,
                                   int16_t list_top);

/**
 * @brief 每帧更新所有行动画
 * @param list  动画上下文指针
 * @param speed 动画速度（0~99，越大越快）
 * @return true  所有行和高亮框已稳定在目标位置
 * @return false 仍有行在动画中
 */
bool xerintosh_anim_row_list_update(xerintosh_anim_row_list_t *list, float speed);

/**
 * @brief 刷新所有行的目标位置（当 selected/scroll 变化时调用）
 * @param list         动画上下文指针
 * @param selected     当前选中索引
 * @param scroll       列表滚动偏移
 * @param screen_width 屏幕宽度
 * @param item_count   总项目数
 * @note  仅修改 trg 值，实际动画由 update() 驱动
 */
void xerintosh_anim_row_list_refresh(xerintosh_anim_row_list_t *list,
                                      int selected, int scroll,
                                      int16_t screen_width, int item_count);

#ifdef __cplusplus
}
#endif

#endif /* UI_ANIM_ROW_H */
