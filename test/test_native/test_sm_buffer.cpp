/**
 * @file   test_sm_buffer.cpp
 * @brief  TDD — sm_buffer 索引公式等效性验证
 * @note   验证 get_line() 和 get_line_source() 对同一 offset
 *         返回一致的数据（两者使用相同索引公式）。
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include "app/serial_monitor/sm_buffer.h"
}

/* ═══ 索引公式等价性 ═══ */

TEST(SmBufferIndex, GetLineAndGetLineSourceAgree)
{
    sm_buffer_t buf;
    sm_buffer_init(&buf);

    /* 填满缓冲区，每行的 from_host 与内容中的编号一致 */
    for (int i = 0; i < SM_TERM_LINES; i++) {
        char text[16];
        snprintf(text, sizeof(text), "L%03d", i);
        bool host = (i % 3 == 0);  /* 每 3 行一个 from_host=true */
        sm_buffer_add_line(&buf, text, host);
    }

    /*
     * offset=0 指向最近添加的行 (i=SM_TERM_LINES-1),
     * offset=1 指向上一行...以此类推。
     * get_line 和 get_line_source 必须对同一 offset 返回同一逻辑行。
     */
    for (int16_t offset = 0; offset < buf.count; offset++) {
        char line_text[SM_TERM_LINE_LEN];
        sm_buffer_get_line(&buf, offset, line_text, sizeof(line_text));
        bool src = sm_buffer_get_line_source(&buf, offset);

        /* 从行号推算行索引 i */
        int line_num;
        EXPECT_EQ(sscanf(line_text, "L%d", &line_num), 1)
            << "offset=" << offset << " got invalid text";
        bool expected_host = (line_num % 3 == 0);
        EXPECT_EQ(src, expected_host)
            << "offset=" << offset << " line_num=" << line_num
            << ": get_line_source disagrees with known host flag";
    }
}

TEST(SmBufferIndex, WrapAroundPreservesConsistency)
{
    sm_buffer_t buf;
    sm_buffer_init(&buf);

    /* 写入超过缓冲区两倍的行，触发覆盖 */
    for (int i = 0; i < SM_TERM_LINES * 2 + 5; i++) {
        char text[16];
        snprintf(text, sizeof(text), "W%04d", i);
        sm_buffer_add_line(&buf, text, (i % 7 == 0));
    }

    /* 所有有效 offset 下，get_line 和 get_line_source 应对齐 */
    for (int16_t offset = 0; offset < buf.count; offset++) {
        char line_text[SM_TERM_LINE_LEN];
        sm_buffer_get_line(&buf, offset, line_text, sizeof(line_text));
        bool src = sm_buffer_get_line_source(&buf, offset);

        int line_num;
        int n = sscanf(line_text, "W%d", &line_num);
        ASSERT_EQ(n, 1) << "offset=" << offset << " unparseable: " << line_text;
        EXPECT_EQ(src, (line_num % 7 == 0))
            << "offset=" << offset << " line_num=" << line_num;
    }
}
