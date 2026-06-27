#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_stream.h"
#include "kernel/kern_init.h"
}

TEST(KernelStreamTest, SendRecvRoundTrip)
{
    kern_init();
    uint8_t buf[64];
    kern_stream_t s;
    kern_stream_init(&s, buf, sizeof(buf));

    const char *msg = "hello stream";
    size_t sent = kern_stream_send(&s, msg, strlen(msg), 0);
    EXPECT_EQ(sent, strlen(msg));

    char out[64] = {0};
    size_t rcvd = kern_stream_recv(&s, out, strlen(msg), 0);
    EXPECT_EQ(rcvd, strlen(msg));
    EXPECT_STREQ(out, msg);
}

TEST(KernelStreamTest, MsgBufRoundTrip)
{
    kern_init();
    uint8_t buf[64];
    kern_stream_t s;
    kern_stream_init(&s, buf, sizeof(buf));

    const char *msg = "packet";
    EXPECT_EQ(kern_msg_buf_send(&s, msg, strlen(msg), 0), KERN_OK);

    char out[64] = {0};
    size_t rcvd = kern_msg_buf_recv(&s, out, sizeof(out), 0);
    EXPECT_EQ(rcvd, strlen(msg));
    EXPECT_STREQ(out, msg);
}

TEST(KernelStreamTest, PartialSendRespectsCapacity)
{
    kern_init();
    uint8_t buf[4];
    kern_stream_t s;
    kern_stream_init(&s, buf, sizeof(buf));

    const char *msg = "abcdef";
    size_t sent = kern_stream_send(&s, msg, strlen(msg), 0);
    EXPECT_EQ(sent, 4u);
    EXPECT_EQ(kern_stream_bytes_available(&s), 4u);
}
