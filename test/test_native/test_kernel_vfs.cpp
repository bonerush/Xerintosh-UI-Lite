/**
 * @file   test_kernel_vfs.cpp
 * @brief  Xeros 虚拟文件系统（VFS）单元测试
 * @details 测试路径解析、文件打开/关闭/读写、错误处理、
 *          文件描述符表管理。
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "kernel/kern_types.h"
#include "kernel/kern_vfs.h"
#include "kernel/kern_init.h"
}

/* ═══ 简单 inode 工厂（供测试用） ═══ */

static ssize_t mock_read(kern_file_t *f, char *buf, size_t len)
{
    (void)f;
    (void)buf;
    (void)len;
    return 0;
}

static ssize_t mock_write(kern_file_t *f, const char *buf, size_t len)
{
    (void)f;
    (void)buf;
    return (ssize_t)len;  /* 假装写入成功 */
}

static kern_file_ops_t mock_fops = {
    .read = mock_read,
    .write = mock_write,
    .ioctl = NULL,
    .release = NULL,
};

static kern_inode_t *make_mock_inode(kern_file_type_t type)
{
    kern_inode_t *ino = (kern_inode_t *)malloc(sizeof(kern_inode_t));
    if (ino) {
        ino->type = type;
        ino->fops = &mock_fops;
        ino->private_data = NULL;
    }
    return ino;
}

/* ═══ VFS 初始化测试 ═══ */

TEST(KernelVFSTest, VfsInitCreatesRoot)
{
    kern_vfs_init();
    kern_dentry_t *root = kern_vfs_get_root();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->parent, nullptr);
    EXPECT_STREQ(root->name, "/");
}

TEST(KernelVFSTest, VfsInitIsIdempotent)
{
    kern_vfs_init();
    kern_vfs_init();
    kern_dentry_t *root = kern_vfs_get_root();
    ASSERT_NE(root, nullptr);
}

/* ═══ 路径解析测试 ═══ */

TEST(KernelVFSTest, ResolveRootPath)
{
    kern_vfs_init();
    kern_dentry_t *d = kern_path_resolve("/");
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d->name, "/");
}

TEST(KernelVFSTest, ResolveNonexistentPathReturnsNull)
{
    kern_vfs_init();
    kern_dentry_t *d = kern_path_resolve("/nonexistent");
    EXPECT_EQ(d, nullptr);
}

TEST(KernelVFSTest, ResolveNestedNonexistentPathReturnsNull)
{
    kern_vfs_init();
    kern_dentry_t *d = kern_path_resolve("/a/b/c");
    EXPECT_EQ(d, nullptr);
}

TEST(KernelVFSTest, ResolvePathWithTrailingSlash)
{
    kern_vfs_init();
    /* "/" should resolve successfully */
    kern_dentry_t *d = kern_path_resolve("/");
    EXPECT_NE(d, nullptr);

    /* "///" should also resolve to root */
    kern_dentry_t *d2 = kern_path_resolve("///");
    if (d2 != nullptr) {
        EXPECT_STREQ(d2->name, "/");
    }
}

/* ═══ 目录项注册测试 ═══ */

TEST(KernelVFSTest, RegisterSingleEntry)
{
    kern_vfs_init();
    kern_inode_t *ino = make_mock_inode(KERN_FILE_CHRDEV);

    int rc = kern_dentry_register("/test", ino);
    EXPECT_EQ(rc, KERN_OK);

    kern_dentry_t *d = kern_path_resolve("/test");
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d->name, "test");
    EXPECT_NE(d->inode, nullptr);
    EXPECT_EQ(d->inode->type, KERN_FILE_CHRDEV);
}

TEST(KernelVFSTest, RegisterNestedEntry)
{
    kern_vfs_init();
    kern_inode_t *ino = make_mock_inode(KERN_FILE_CHRDEV);

    int rc = kern_dentry_register("/dev/null", ino);
    EXPECT_EQ(rc, KERN_OK);

    /* 中间目录 /dev 应该被自动创建 */
    kern_dentry_t *dev = kern_path_resolve("/dev");
    ASSERT_NE(dev, nullptr);
    EXPECT_STREQ(dev->name, "dev");
    EXPECT_EQ(dev->inode, nullptr);  /* 中间目录没有 inode */

    /* 叶节点 /dev/null 应该存在 */
    kern_dentry_t *null_entry = kern_path_resolve("/dev/null");
    ASSERT_NE(null_entry, nullptr);
    EXPECT_STREQ(null_entry->name, "null");
    EXPECT_NE(null_entry->inode, nullptr);
}

TEST(KernelVFSTest, RegisterDuplicatePathReplaces)
{
    kern_vfs_init();
    kern_inode_t *ino1 = make_mock_inode(KERN_FILE_CHRDEV);
    kern_inode_t *ino2 = make_mock_inode(KERN_FILE_REGULAR);

    kern_dentry_register("/dup", ino1);
    kern_dentry_register("/dup", ino2);

    /* 第二次注册应替换 */
    kern_dentry_t *d = kern_path_resolve("/dup");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->inode, ino2);
}

TEST(KernelVFSTest, RegisterNullPathReturnsError)
{
    kern_vfs_init();
    kern_inode_t *ino = make_mock_inode(KERN_FILE_CHRDEV);

    int rc = kern_dentry_register(NULL, ino);
    EXPECT_LT(rc, 0);
}

TEST(KernelVFSTest, RegisterNullInodeReturnsError)
{
    kern_vfs_init();
    int rc = kern_dentry_register("/null_inode", NULL);
    EXPECT_LT(rc, 0);
}

/* ═══ 文件打开测试 ═══ */

TEST(KernelVFSTest, OpenExistingFile)
{
    kern_vfs_init();
    kern_inode_t *ino = make_mock_inode(KERN_FILE_REGULAR);
    kern_dentry_register("/file", ino);

    kern_fd_t fd = kern_open("/file", KERN_O_RDONLY);
    EXPECT_GE(fd, 0);
    kern_close(fd);
}

TEST(KernelVFSTest, OpenNonexistentFileReturnsError)
{
    kern_vfs_init();
    kern_fd_t fd = kern_open("/nonexistent", KERN_O_RDONLY);
    EXPECT_LT(fd, 0);
}

TEST(KernelVFSTest, OpenReturnsUniqueFds)
{
    kern_vfs_init();
    kern_inode_t *ino = make_mock_inode(KERN_FILE_REGULAR);
    kern_dentry_register("/file1", ino);
    kern_inode_t *ino2 = make_mock_inode(KERN_FILE_REGULAR);
    kern_dentry_register("/file2", ino2);

    kern_fd_t fd1 = kern_open("/file1", KERN_O_RDONLY);
    kern_fd_t fd2 = kern_open("/file2", KERN_O_RDONLY);
    EXPECT_GE(fd1, 0);
    EXPECT_GE(fd2, 0);
    EXPECT_NE(fd1, fd2);

    kern_close(fd1);
    kern_close(fd2);
}

TEST(KernelVFSTest, CloseInvalidFdReturnsError)
{
    kern_vfs_init();
    int rc = kern_close(999);
    EXPECT_EQ(rc, KERN_EBADF);
}

TEST(KernelVFSTest, DoubleCloseReturnsError)
{
    kern_vfs_init();
    kern_inode_t *ino = make_mock_inode(KERN_FILE_REGULAR);
    kern_dentry_register("/double_close", ino);

    kern_fd_t fd = kern_open("/double_close", KERN_O_RDONLY);
    EXPECT_GE(fd, 0);
    EXPECT_EQ(kern_close(fd), KERN_OK);
    EXPECT_EQ(kern_close(fd), KERN_EBADF);
}

/* ═══ 文件读写测试 ═══ */

TEST(KernelVFSTest, ReadWithoutFopsReturnsError)
{
    kern_vfs_init();
    kern_inode_t *ino = make_mock_inode(KERN_FILE_REGULAR);
    ino->fops = NULL;  /* 没有 fops */
    kern_dentry_register("/nofops", ino);

    kern_fd_t fd = kern_open("/nofops", KERN_O_RDONLY);
    EXPECT_GE(fd, 0);

    char buf[16];
    ssize_t n = kern_read(fd, buf, sizeof(buf));
    EXPECT_EQ(n, KERN_EINVAL);

    kern_close(fd);
}

TEST(KernelVFSTest, ReadFromInvalidFdReturnsError)
{
    kern_vfs_init();
    char buf[16];
    ssize_t n = kern_read(999, buf, sizeof(buf));
    EXPECT_EQ(n, KERN_EBADF);
}

TEST(KernelVFSTest, WriteViaFops)
{
    kern_vfs_init();
    kern_inode_t *ino = make_mock_inode(KERN_FILE_REGULAR);
    kern_dentry_register("/writable", ino);

    kern_fd_t fd = kern_open("/writable", KERN_O_WRONLY);
    EXPECT_GE(fd, 0);

    ssize_t n = kern_write(fd, "hello", 5);
    EXPECT_EQ(n, 5);  /* mock_write 返回写入长度 */

    kern_close(fd);
}

TEST(KernelVFSTest, WriteToInvalidFdReturnsError)
{
    kern_vfs_init();
    ssize_t n = kern_write(999, "hello", 5);
    EXPECT_EQ(n, KERN_EBADF);
}

/* ═══ FD 表溢出测试 ═══ */

TEST(KernelVFSTest, MaxFdPerTask)
{
    kern_vfs_init();
    kern_inode_t *ino = make_mock_inode(KERN_FILE_REGULAR);
    kern_dentry_register("/fdtest", ino);

    /* 打开 KERN_MAX_FD_PER_TASK 个文件 */
    kern_fd_t fds[KERN_MAX_FD_PER_TASK];
    for (int i = 0; i < KERN_MAX_FD_PER_TASK; i++) {
        fds[i] = kern_open("/fdtest", KERN_O_RDONLY);
        EXPECT_GE(fds[i], 0) << "fd " << i << " should be valid";
    }

    /* 再打开一个应该失败 */
    kern_fd_t fd_overflow = kern_open("/fdtest", KERN_O_RDONLY);
    EXPECT_EQ(fd_overflow, KERN_EMFILE);

    /* 关闭所有 */
    for (int i = 0; i < KERN_MAX_FD_PER_TASK; i++) {
        kern_close(fds[i]);
    }
}

/* ═══ 读写无效 FD 测试 ═══ */

TEST(KernelVFSTest, IoctlInvalidFdReturnsError)
{
    kern_vfs_init();
    int rc = kern_ioctl(999, 0, 0);
    EXPECT_EQ(rc, KERN_EBADF);
}

TEST(KernelVFSTest, IoctlWithoutFopsReturnsError)
{
    kern_vfs_init();
    kern_inode_t *ino = make_mock_inode(KERN_FILE_REGULAR);
    ino->fops = NULL;
    kern_dentry_register("/noioctl", ino);

    kern_fd_t fd = kern_open("/noioctl", KERN_O_RDWR);
    EXPECT_GE(fd, 0);
    int rc = kern_ioctl(fd, 1, 0);
    EXPECT_EQ(rc, KERN_EINVAL);
    kern_close(fd);
}
