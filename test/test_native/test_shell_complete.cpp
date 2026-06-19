/**
 * @file   test_shell_complete.cpp
 * @brief  Shell Tab 补全单元测试
 * @details 测试 shell_token_start、shell_parent_path 和 shell_complete_path
 *          的路径拆分、单匹配补全、多匹配公共前缀补全等行为。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_vfs.h"
#include "kernel/kern_sched.h"
#include "kernel/kern_init.h"
#include "kernel/kern_shell_complete.h"
}

/* ═══ 辅助：mock 输出设备 ═══ */

static ssize_t null_write(kern_file_t *f, const char *buf, size_t len)
{
    (void)f;
    (void)buf;
    return (ssize_t)len;
}

static kern_file_ops_t null_fops = {
    .read = NULL,
    .write = null_write,
    .ioctl = NULL,
    .release = NULL,
};

class ShellCompleteTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        kern_clear_panic();
        kern_sched_init();
        kern_vfs_init();
    }

    kern_fd_t open_null_fd(void)
    {
        kern_inode_t *ino = (kern_inode_t *)calloc(1, sizeof(kern_inode_t));
        ino->type = KERN_FILE_CHRDEV;
        ino->fops = &null_fops;
        ino->private_data = NULL;
        ino->ref_count = 0;

        kern_err_t rc = kern_dentry_register("/dev/null_test", ino);
        EXPECT_EQ(rc, KERN_OK);
        if (rc != KERN_OK) {
            return -1;
        }
        return kern_open("/dev/null_test", KERN_O_WRONLY);
    }

    kern_inode_t *make_inode(kern_file_type_t type)
    {
        kern_inode_t *ino = (kern_inode_t *)calloc(1, sizeof(kern_inode_t));
        ino->type = type;
        ino->fops = &null_fops;
        ino->private_data = NULL;
        ino->ref_count = 0;
        return ino;
    }
};

/* ═══ shell_token_start 测试 ═══ */

TEST_F(ShellCompleteTest, TokenStartEmpty)
{
    const char *line = "";
    EXPECT_EQ(shell_token_start(line, 0), (size_t)0);
}

TEST_F(ShellCompleteTest, TokenStartNoDelimiter)
{
    const char *line = "ls";
    EXPECT_EQ(shell_token_start(line, 2), (size_t)0);
}

TEST_F(ShellCompleteTest, TokenStartSpace)
{
    const char *line = "ls /dev";
    EXPECT_EQ(shell_token_start(line, 7), (size_t)3);
}

TEST_F(ShellCompleteTest, TokenStartTab)
{
    const char *line = "ls\t/dev";
    EXPECT_EQ(shell_token_start(line, 7), (size_t)3);
}

/* ═══ shell_parent_path 测试 ═══ */

TEST_F(ShellCompleteTest, ParentPathEmptyPrefix)
{
    char dir[KERN_PATH_MAX];
    const char *base = nullptr;
    EXPECT_TRUE(shell_parent_path("/", "", dir, sizeof(dir), &base));
    EXPECT_STREQ(dir, "/");
    EXPECT_STREQ(base, "");
}

TEST_F(ShellCompleteTest, ParentPathRelativeNoSlash)
{
    char dir[KERN_PATH_MAX];
    const char *base = nullptr;
    EXPECT_TRUE(shell_parent_path("/", "dev", dir, sizeof(dir), &base));
    EXPECT_STREQ(dir, "/");
    EXPECT_STREQ(base, "dev");
}

TEST_F(ShellCompleteTest, ParentPathRelativeWithSlashFromRoot)
{
    char dir[KERN_PATH_MAX];
    const char *base = nullptr;
    EXPECT_TRUE(shell_parent_path("/", "dev/in", dir, sizeof(dir), &base));
    EXPECT_STREQ(dir, "/dev");
    EXPECT_STREQ(base, "in");
}

TEST_F(ShellCompleteTest, ParentPathRelativeWithSlashFromSubdir)
{
    char dir[KERN_PATH_MAX];
    const char *base = nullptr;
    EXPECT_TRUE(shell_parent_path("/sys", "dev/in", dir, sizeof(dir), &base));
    EXPECT_STREQ(dir, "/sys/dev");
    EXPECT_STREQ(base, "in");
}

TEST_F(ShellCompleteTest, ParentPathAbsoluteRoot)
{
    char dir[KERN_PATH_MAX];
    const char *base = nullptr;
    EXPECT_TRUE(shell_parent_path("/", "/", dir, sizeof(dir), &base));
    EXPECT_STREQ(dir, "/");
    EXPECT_STREQ(base, "");
}

TEST_F(ShellCompleteTest, ParentPathAbsoluteNested)
{
    char dir[KERN_PATH_MAX];
    const char *base = nullptr;
    EXPECT_TRUE(shell_parent_path("/", "/dev/in", dir, sizeof(dir), &base));
    EXPECT_STREQ(dir, "/dev");
    EXPECT_STREQ(base, "in");
}

TEST_F(ShellCompleteTest, ParentPathAbsoluteTrailingSlash)
{
    char dir[KERN_PATH_MAX];
    const char *base = nullptr;
    EXPECT_TRUE(shell_parent_path("/", "/dev/", dir, sizeof(dir), &base));
    EXPECT_STREQ(dir, "/dev");
    EXPECT_STREQ(base, "");
}

TEST_F(ShellCompleteTest, ParentPathOverlongBase)
{
    char dir[KERN_PATH_MAX];
    const char *base = nullptr;
    char long_base[KERN_NAME_MAX + 2];
    memset(long_base, 'a', sizeof(long_base) - 1);
    long_base[sizeof(long_base) - 1] = '\0';
    EXPECT_FALSE(shell_parent_path("/", long_base, dir, sizeof(dir), &base));
}

/* ═══ shell_complete_path 测试 ═══
 *
 * 注意：VFS 在 native 测试中为全局状态，kern_vfs_init() 不会重置根 dentry。
 * 为避免与其他测试共享的 /dev 子项互相干扰，每个用例使用独立的
 * /dev/shell_testN/ 子目录，确保补全目标唯一且可预测。
 */

TEST_F(ShellCompleteTest, CompleteSingleFile)
{
    ASSERT_EQ(kern_dentry_register("/dev/shell_test1/fb0", make_inode(KERN_FILE_CHRDEV)), KERN_OK);
    kern_fd_t tty = open_null_fd();
    ASSERT_GE(tty, 0);

    char line[128] = "cat /dev/shell_test1/fb";
    size_t pos = strlen(line);
    shell_complete_path(tty, line, &pos, 4, "/");

    EXPECT_STREQ(line, "cat /dev/shell_test1/fb0 ");
    EXPECT_EQ(pos, strlen("cat /dev/shell_test1/fb0 "));

    kern_close(tty);
}

TEST_F(ShellCompleteTest, CompleteSingleDir)
{
    ASSERT_EQ(kern_dentry_register("/dev/shell_test2/sub/dir", make_inode(KERN_FILE_DIR)), KERN_OK);
    kern_fd_t tty = open_null_fd();
    ASSERT_GE(tty, 0);

    char line[128] = "ls /dev/shell_test2/su";
    size_t pos = strlen(line);
    shell_complete_path(tty, line, &pos, 3, "/");

    /* 目录补全后追加 '/'，但不追加空格 */
    EXPECT_STREQ(line, "ls /dev/shell_test2/sub/");
    EXPECT_EQ(pos, strlen("ls /dev/shell_test2/sub/"));

    kern_close(tty);
}

TEST_F(ShellCompleteTest, CompleteCommonPrefix)
{
    ASSERT_EQ(kern_dentry_register("/dev/shell_test3/fb0", make_inode(KERN_FILE_CHRDEV)), KERN_OK);
    ASSERT_EQ(kern_dentry_register("/dev/shell_test3/input0", make_inode(KERN_FILE_CHRDEV)), KERN_OK);
    ASSERT_EQ(kern_dentry_register("/dev/shell_test3/ttyS0", make_inode(KERN_FILE_CHRDEV)), KERN_OK);
    kern_fd_t tty = open_null_fd();
    ASSERT_GE(tty, 0);

    char line[128] = "cat /dev/shell_test3/";
    size_t pos = strlen(line);
    shell_complete_path(tty, line, &pos, 4, "/");

    /* 多个匹配但无公共前缀，应列出候选而不修改 line */
    EXPECT_STREQ(line, "cat /dev/shell_test3/");
    EXPECT_EQ(pos, strlen("cat /dev/shell_test3/"));

    kern_close(tty);
}

TEST_F(ShellCompleteTest, CompleteCommonPrefixPartial)
{
    ASSERT_EQ(kern_dentry_register("/dev/shell_test4/fb0", make_inode(KERN_FILE_CHRDEV)), KERN_OK);
    ASSERT_EQ(kern_dentry_register("/dev/shell_test4/fb1", make_inode(KERN_FILE_CHRDEV)), KERN_OK);
    ASSERT_EQ(kern_dentry_register("/dev/shell_test4/input0", make_inode(KERN_FILE_CHRDEV)), KERN_OK);
    kern_fd_t tty = open_null_fd();
    ASSERT_GE(tty, 0);

    char line[128] = "cat /dev/shell_test4/f";
    size_t pos = strlen(line);
    shell_complete_path(tty, line, &pos, 4, "/");

    /* fb0 和 fb1 的公共前缀是 "fb"，应补全为 /dev/shell_test4/fb */
    EXPECT_STREQ(line, "cat /dev/shell_test4/fb");
    EXPECT_EQ(pos, strlen("cat /dev/shell_test4/fb"));

    kern_close(tty);
}

TEST_F(ShellCompleteTest, CompleteRelativeFromRoot)
{
    ASSERT_EQ(kern_dentry_register("/dev/shell_test5/fb0", make_inode(KERN_FILE_CHRDEV)), KERN_OK);
    kern_fd_t tty = open_null_fd();
    ASSERT_GE(tty, 0);

    char line[128] = "cat dev/shell_test5/f";
    size_t pos = strlen(line);
    shell_complete_path(tty, line, &pos, 4, "/");

    EXPECT_STREQ(line, "cat dev/shell_test5/fb0 ");
    EXPECT_EQ(pos, strlen("cat dev/shell_test5/fb0 "));

    kern_close(tty);
}

TEST_F(ShellCompleteTest, CompleteNoMatch)
{
    ASSERT_EQ(kern_dentry_register("/dev/shell_test6/fb0", make_inode(KERN_FILE_CHRDEV)), KERN_OK);
    kern_fd_t tty = open_null_fd();
    ASSERT_GE(tty, 0);

    char line[128] = "cat /dev/shell_test6/xyz";
    size_t pos = strlen(line);
    shell_complete_path(tty, line, &pos, 4, "/");

    /* 无匹配：line 和 pos 应保持不变 */
    EXPECT_STREQ(line, "cat /dev/shell_test6/xyz");
    EXPECT_EQ(pos, strlen("cat /dev/shell_test6/xyz"));

    kern_close(tty);
}
