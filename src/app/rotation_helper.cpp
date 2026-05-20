#include <stdint.h>

/**
 * @brief 将存储的屏幕旋转值解析为新的 level 格式。
 *
 * 旧格式: 0=portrait(0deg), 1=landscape(90deg), 2=portrait(180deg), 3=landscape(270deg)
 * 新格式: 1=portrait(0deg), 2=landscape(90deg)
 *
 * 由于新旧格式在值 1 和 2 上有重叠，优先按旧格式映射以兼容旧数据。
 * 设备首次启动并保存新值后，后续读取将直接获得新格式值。
 */
extern "C" int16_t resolve_rotation_level(uint8_t saved_rot)
{
    /* 旧格式 landscape → 新格式 landscape */
    if (saved_rot == 1 || saved_rot == 3)
        return 2;

    /* 旧格式 portrait → 新格式 portrait */
    if (saved_rot == 0 || saved_rot == 2)
        return 1;

    /* 明确的未知值，默认 landscape */
    return 2;
}
