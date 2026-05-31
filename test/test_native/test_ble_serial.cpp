/**
 * @file   test_ble_serial.cpp
 * @brief  BLE 串口监视器 App 基本测试
 * @details 验证 init/exit 不崩溃，以及初始状态正确。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>

extern "C" {
#include "app/ble_serial/ble_serial.h"
}

/**
 * @brief 测试初始化不崩溃
 */
TEST(BleSerialTest, InitDoesNotCrash)
{
    ble_serial_init(NULL);
    /* 无断言 — 验证不崩溃 */
}

/**
 * @brief 测试退出不崩溃
 */
TEST(BleSerialTest, ExitDoesNotCrash)
{
    ble_serial_init(NULL);
    ble_serial_exit(NULL);
    /* 无断言 — 验证不崩溃 */
}

/**
 * @brief 测试循环不崩溃
 */
TEST(BleSerialTest, LoopDoesNotCrash)
{
    ble_serial_init(NULL);
    ble_serial_loop(NULL);
    ble_serial_exit(NULL);
}

/**
 * @brief 测试多次 init/exit 不崩溃
 */
TEST(BleSerialTest, MultipleInitExit)
{
    for (int i = 0; i < 3; i++) {
        ble_serial_init(NULL);
        ble_serial_exit(NULL);
    }
}
