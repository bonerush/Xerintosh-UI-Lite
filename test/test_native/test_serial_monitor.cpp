#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include "app/serial_monitor.h"

/* 前向声明内部缓冲区函数（仅供测试使用） */
#define SM_TERM_LINES    20
#define SM_TERM_LINE_LEN 64

typedef struct {
    char text[SM_TERM_LINE_LEN];
    bool from_host;
} sm_line_t;

typedef struct {
    sm_line_t lines[SM_TERM_LINES];
    uint8_t head;
    uint8_t count;
    int16_t scroll;
} sm_buffer_t;

void sm_buffer_init(sm_buffer_t *buf);
bool sm_buffer_add_line(sm_buffer_t *buf, const char *text, bool from_host);
void sm_buffer_get_line(const sm_buffer_t *buf, int16_t offset,
                         char *out, size_t out_len);
bool sm_buffer_get_line_source(const sm_buffer_t *buf, int16_t offset);
void sm_buffer_clear(sm_buffer_t *buf);
}

/* ═══ Phase 3: 串口监视器缓冲区测试 ═══ */

TEST(SerialMonitorBufferTest, InitClearsBuffer)
{
    sm_buffer_t buf;
    sm_buffer_init(&buf);
    EXPECT_EQ(buf.head, 0);
    EXPECT_EQ(buf.count, 0);
    EXPECT_EQ(buf.scroll, 0);
}

TEST(SerialMonitorBufferTest, AddLineAndRetrieve)
{
    sm_buffer_t buf;
    sm_buffer_init(&buf);

    EXPECT_TRUE(sm_buffer_add_line(&buf, "Hello", true));
    EXPECT_EQ(buf.count, 1);

    char out[64];
    sm_buffer_get_line(&buf, 0, out, sizeof(out));
    EXPECT_STREQ(out, "Hello");

    /* 验证 source 查询 */
    EXPECT_TRUE(sm_buffer_get_line_source(&buf, 0));
}

TEST(SerialMonitorBufferTest, AddMultipleLines)
{
    sm_buffer_t buf;
    sm_buffer_init(&buf);

    EXPECT_TRUE(sm_buffer_add_line(&buf, "Line1", true));
    EXPECT_TRUE(sm_buffer_add_line(&buf, "Line2", false));
    EXPECT_TRUE(sm_buffer_add_line(&buf, "Line3", true));

    char out[64];
    sm_buffer_get_line(&buf, 0, out, sizeof(out));
    EXPECT_STREQ(out, "Line3");  /* 最新行 */
    EXPECT_TRUE(sm_buffer_get_line_source(&buf, 0));

    sm_buffer_get_line(&buf, 1, out, sizeof(out));
    EXPECT_STREQ(out, "Line2");  /* 次新行 */
    EXPECT_FALSE(sm_buffer_get_line_source(&buf, 1));

    sm_buffer_get_line(&buf, 2, out, sizeof(out));
    EXPECT_STREQ(out, "Line1");  /* 最旧行 */
    EXPECT_TRUE(sm_buffer_get_line_source(&buf, 2));
}

TEST(SerialMonitorBufferTest, BufferWrapsAround)
{
    sm_buffer_t buf;
    sm_buffer_init(&buf);

    /* 添加超过 SM_TERM_LINES 的行 */
    for (int i = 0; i < 25; i++) {
        char text[16];
        snprintf(text, sizeof(text), "L%d", i);
        EXPECT_TRUE(sm_buffer_add_line(&buf, text, true));
    }

    /* 缓冲区已满，count 应为 SM_TERM_LINES */
    EXPECT_EQ(buf.count, SM_TERM_LINES);

    /* 验证最旧行被覆盖：最新的是 L24，最旧保留的是 L5 */
    char out[64];
    sm_buffer_get_line(&buf, 0, out, sizeof(out));
    EXPECT_STREQ(out, "L24");   /* 最新 */
    EXPECT_TRUE(sm_buffer_get_line_source(&buf, 0));

    sm_buffer_get_line(&buf, SM_TERM_LINES - 1, out, sizeof(out));
    EXPECT_STREQ(out, "L5");    /* 最旧保留 */
    EXPECT_TRUE(sm_buffer_get_line_source(&buf, SM_TERM_LINES - 1));
}

TEST(SerialMonitorBufferTest, ClearRemovesAll)
{
    sm_buffer_t buf;
    sm_buffer_init(&buf);

    sm_buffer_add_line(&buf, "Test", true);
    EXPECT_EQ(buf.count, 1);

    sm_buffer_clear(&buf);
    EXPECT_EQ(buf.count, 0);

    char out[64];
    sm_buffer_get_line(&buf, 0, out, sizeof(out));
    EXPECT_STREQ(out, "");
}

TEST(SerialMonitorBufferTest, OffsetOutOfRangeReturnsEmpty)
{
    sm_buffer_t buf;
    sm_buffer_init(&buf);

    sm_buffer_add_line(&buf, "OnlyOne", true);

    char out[64];
    sm_buffer_get_line(&buf, 0, out, sizeof(out));
    EXPECT_STREQ(out, "OnlyOne");
    EXPECT_TRUE(sm_buffer_get_line_source(&buf, 0));

    sm_buffer_get_line(&buf, 1, out, sizeof(out));
    EXPECT_STREQ(out, "");

    sm_buffer_get_line(&buf, -1, out, sizeof(out));
    EXPECT_STREQ(out, "");
}

TEST(SerialMonitorBufferTest, GetLineSourceOutOfRangeReturnsFalse)
{
    sm_buffer_t buf;
    sm_buffer_init(&buf);

    sm_buffer_add_line(&buf, "Test", false);
    EXPECT_FALSE(sm_buffer_get_line_source(&buf, 0));

    /* 超出范围返回 false */
    EXPECT_FALSE(sm_buffer_get_line_source(&buf, 1));
    EXPECT_FALSE(sm_buffer_get_line_source(&buf, -1));
}

TEST(SerialMonitorBufferTest, LongTextTruncated)
{
    sm_buffer_t buf;
    sm_buffer_init(&buf);

    /* 构造超过 SM_TERM_LINE_LEN 的字符串 */
    char long_text[128];
    memset(long_text, 'A', sizeof(long_text));
    long_text[sizeof(long_text) - 1] = '\0';

    EXPECT_TRUE(sm_buffer_add_line(&buf, long_text, false));

    char out[128];
    sm_buffer_get_line(&buf, 0, out, sizeof(out));

    /* 验证无前缀，内容被截断到 SM_TERM_LINE_LEN - 1 */
    EXPECT_EQ(strlen(out), (size_t)(SM_TERM_LINE_LEN - 1));
    EXPECT_FALSE(sm_buffer_get_line_source(&buf, 0));
}

TEST(SerialMonitorBufferTest, NullPointerSafety)
{
    /* 验证空指针不会崩溃 */
    EXPECT_FALSE(sm_buffer_add_line(nullptr, "test", true));
    EXPECT_FALSE(sm_buffer_add_line(nullptr, nullptr, true));
}
