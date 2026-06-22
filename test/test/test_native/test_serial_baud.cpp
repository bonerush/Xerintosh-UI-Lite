#include <gtest/gtest.h>

extern "C" {
#include "app/settings/settings.h"
}

/* ═══ Phase 2: 波特率等级映射测试 ═══ */

/**
 * @brief 有效等级应正确映射到波特率值
 */
TEST(SerialBaudTest, LevelMapsToCorrectBaudRate)
{
    EXPECT_EQ(settings_serial_baud_hw_value(1), 9600);
    EXPECT_EQ(settings_serial_baud_hw_value(2), 19200);
    EXPECT_EQ(settings_serial_baud_hw_value(3), 38400);
    EXPECT_EQ(settings_serial_baud_hw_value(4), 57600);
    EXPECT_EQ(settings_serial_baud_hw_value(5), 115200);
    EXPECT_EQ(settings_serial_baud_hw_value(6), 230400);
}

/**
 * @brief 无效低等级应回退到默认值 115200
 */
TEST(SerialBaudTest, InvalidLowLevelFallsBackToDefault)
{
    EXPECT_EQ(settings_serial_baud_hw_value(0), 115200);
    EXPECT_EQ(settings_serial_baud_hw_value(-1), 115200);
}

/**
 * @brief 无效高等级应回退到最大值 230400
 */
TEST(SerialBaudTest, InvalidHighLevelFallsBackToMax)
{
    EXPECT_EQ(settings_serial_baud_hw_value(7), 230400);
    EXPECT_EQ(settings_serial_baud_hw_value(100), 230400);
}

/**
 * @brief 全局变量默认值为 5（115200）
 */
TEST(SerialBaudTest, GlobalDefaultIsLevel5)
{
    EXPECT_EQ(g_serial_baud_rate, 5);
}

/**
 * @brief 波特率映射表访问器与 settings_serial_baud_hw_value 语义一致
 */
TEST(SerialBaudTest, BaudTableMatchesHwValue)
{
    const int32_t *table = settings_serial_baud_table();
    int count = settings_serial_baud_count();
    EXPECT_EQ(count, 6);
    for (int i = 0; i < count; i++) {
        EXPECT_EQ(table[i], settings_serial_baud_hw_value((int16_t)(i + 1)));
    }
}
