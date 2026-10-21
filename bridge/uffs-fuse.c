/*
  This is a simple bridge layer to mount UFFS file system on fuse
  compatible operating systems.

  Copyright (C) 2026-2026 Leonardo Sinibaldi <leonardosinibaldi21@gmail.com>

  uffs-fuse and UFFS are free software; you can redistribute it and/or modify it under
  the GNU Library General Public License as published by the Free Software
  Foundation; either version 2 of the License, or (at your option) any
  later version.

  uffs-fuse and UFFS are distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
  or GNU Library General Public License, as applicable, for more details.

  You should have received a copy of the GNU General Public License
  and GNU Library General Public License along with UFFS; if not, write
  to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA  02110-1301, USA.

  As a special exception, if other files instantiate templates or use
  macros or inline functions from this file, or you compile this file
  and link it with other works to produce a work based on this file,
  this file does not by itself cause the resulting work to be covered
  by the GNU General Public License. However the source code for this
  file must still be made available in accordance with section (3) of
  the GNU General Public License v2.

  This exception does not invalidate any other reasons why a work based
  on this file might be covered by the GNU General Public License.
*/



#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#undef st_atime
#undef st_mtime
#undef st_ctime

#include "uffs_public.h"
#include "uffs_mtb.h"
#include "uffs_fd.h"
#include "uffs_config.h"
#include "emu/uffs_fileem.h"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
#define UFFS_LOCK()   pthread_mutex_lock(&g_lock)
#define UFFS_UNLOCK() pthread_mutex_unlock(&g_lock)

/* UFS path normalization */

static void make_file_path(const char *fuse_path, char *out, size_t out_len) {
    strncpy(out, fuse_path, out_len-1);
    out[out_len-1] = 0;
}

static void make_dir_path(const char *fuse_path, char *out, size_t out_len) {
    size_t len = strlen(fuse_path);
    if(len + 2 >= out_len) {
        out[0] = 0;
        return;
    }

    strcpy(out, fuse_path);
    if(out[len - 1] != '/') {
        out[len] = '/';
        out[len + 1] = 0;
    }
}


/* FUSE callbacks */

static int uf_getattr(const char *path, struct stat *st) {
    memset(st, 0, sizeof(*st));

    if(strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        return 0;
    }

    const char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;
    if (strncmp(basename, "._", 2) == 0) return -ENOENT;
    if (strcmp(basename, ".DS_Store") == 0) return -ENOENT;

    if (path[1] == '.') return -ENOENT;

    UFFS_LOCK();

    struct uffs_stat ustat;
    char fpath[256];
    char dpath[256];

    make_file_path(path, fpath, sizeof(fpath));
    make_dir_path(path, dpath, sizeof(dpath));

    int ret = -ENOENT;

    if (uffs_stat(dpath, &ustat) == 0 && (ustat.st_mode & US_IFDIR)) {
        // It's a directory
        st->st_mode  = S_IFDIR | 0755;
        st->st_nlink = 2;
        ret = 0;
    } else if (uffs_stat(fpath, &ustat) == 0) {
        // It's a file
        st->st_mode  = S_IFREG | 0644;
        st->st_nlink = 1;
        st->st_size  = ustat.st_size;
        ret = 0;
    }


    UFFS_UNLOCK();
    return ret;

}

static int uf_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi) {
    (void)offset; (void)fi;

    char dpath[256];
    make_dir_path(path, dpath, sizeof(dpath));


    UFFS_LOCK();

    uffs_DIR *dir = uffs_opendir(dpath);
    if (!dir) { UFFS_UNLOCK(); return -ENOENT; }

    filler(buf, ".",  NULL, 0);
    filler(buf, "..", NULL, 0);

    struct uffs_dirent *ent;
    while ((ent = uffs_readdir(dir)) != NULL) {
        char name[256];
        strncpy(name, ent->d_name, sizeof(name) - 1);
        size_t len = strlen(name);
        if (len > 0 && name[len - 1] == '/')
            name[len - 1] = '\0';
        filler(buf, name, NULL, 0);
    }

    uffs_closedir(dir);
    UFFS_UNLOCK();
    return 0;
}


static int uf_open(const char *path, struct fuse_file_info *fi) {
    const char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;
    if (strncmp(basename, "._", 2) == 0) return -ENOENT;
    if (strcmp(basename, ".DS_Store") == 0) return -ENOENT;

    char fpath[256];
    make_file_path(path, fpath, sizeof(fpath));

    UFFS_LOCK();

    int flags = UO_RDONLY;
    if ((fi->flags & O_ACCMODE) == O_WRONLY)  flags = UO_WRONLY;
    if ((fi->flags & O_ACCMODE) == O_RDWR)    flags = UO_RDWR;
    if (fi->flags & O_CREAT)                  flags |= UO_CREATE;
    if (fi->flags & O_APPEND)                 flags |= UO_APPEND;
    if (fi->flags & O_TRUNC)                  flags |= UO_TRUNC;

    int fd = uffs_open(fpath, flags);
    UFFS_UNLOCK();

    if (fd < 0) return -ENOENT;
    fi->fh = (uint64_t)fd;
    return 0;
}

static int uf_create(const char *path, mode_t mode,
                     struct fuse_file_info *fi) {
    (void)mode;

    const char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;
    if (strncmp(basename, "._", 2) == 0) return -EPERM;

    char fpath[256];
    make_file_path(path, fpath, sizeof(fpath));

    UFFS_LOCK();
    int fd = uffs_open(fpath, UO_RDWR | UO_CREATE | UO_TRUNC);
    UFFS_UNLOCK();

    if (fd < 0) return -EIO;
    fi->fh = (uint64_t)fd;
    return 0;
}

static int uf_release(const char *path, struct fuse_file_info *fi) {
    (void)path;
    UFFS_LOCK();
    uffs_close((int)fi->fh);
    UFFS_UNLOCK();
    return 0;
}

static int uf_read(const char *path, char *buf, size_t size,
                   off_t offset, struct fuse_file_info *fi) {
    (void)path;
    UFFS_LOCK();
    uffs_seek((int)fi->fh, (int)offset, USEEK_SET);
    int n = uffs_read((int)fi->fh, buf, (int)size);
    UFFS_UNLOCK();
    return n < 0 ? -EIO : n;
}

static int uf_write(const char *path, const char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) {
    (void)path;

    const char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;
    if (strncmp(basename, "._", 2) == 0) return -ENOENT;
    if (strcmp(basename, ".DS_Store") == 0) return -ENOENT;

    UFFS_LOCK();
    uffs_seek((int)fi->fh, (int)offset, USEEK_SET);
    int n = uffs_write((int)fi->fh, buf, (int)size);
    UFFS_UNLOCK();
    return n < 0 ? -EIO : n;
}

static int uf_mkdir(const char *path, mode_t mode) {
    (void)mode;

    const char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;
    if (strncmp(basename, "._", 2) == 0) return -EPERM;

    char dpath[256];
    make_dir_path(path, dpath, sizeof(dpath));

    UFFS_LOCK();
    int ret = uffs_mkdir(dpath);
    UFFS_UNLOCK();
    return ret < 0 ? -EIO : 0;
}

static int uf_rmdir(const char *path) {
    char dpath[256];
    make_dir_path(path, dpath, sizeof(dpath));

    UFFS_LOCK();
    int ret = uffs_rmdir(dpath);
    UFFS_UNLOCK();
    return ret < 0 ? -ENOTEMPTY : 0;
}

static int uf_unlink(const char *path) {
    const char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;
    if (strncmp(basename, "._", 2) == 0) return -ENOENT;
    if (strcmp(basename, ".DS_Store") == 0) return -ENOENT;

    char fpath[256];
    make_file_path(path, fpath, sizeof(fpath));

    UFFS_LOCK();
    int ret = uffs_remove(fpath);
    UFFS_UNLOCK();
    return ret < 0 ? -ENOENT : 0;
}

static int uf_rename(const char *from, const char *to) {
    char f1[256], f2[256];
    make_file_path(from, f1, sizeof(f1));
    make_file_path(to,   f2, sizeof(f2));

    UFFS_LOCK();
    int ret = uffs_rename(f1, f2);
    UFFS_UNLOCK();
    return ret < 0 ? -EIO : 0;
}

static int uf_truncate(const char *path, off_t size) {
    char fpath[256];
    make_file_path(path, fpath, sizeof(fpath));

    UFFS_LOCK();
    int fd = uffs_open(fpath, UO_WRONLY);
    if (fd < 0) { UFFS_UNLOCK(); return -ENOENT; }
    uffs_ftruncate(fd, (long)size);
    uffs_close(fd);
    UFFS_UNLOCK();
    return 0;
}

static int uf_chmod(const char *path, mode_t mode) {
    (void)path; (void)mode;
    return 0;
}

static int uf_chown(const char *path, uid_t uid, gid_t gid) {
    (void)path; (void)uid; (void)gid;
    return 0;
}

#if defined(__APPLE__)
// macFUSE specific signatures for extended attributes
static int uf_setxattr(const char *path, const char *name, const char *value, size_t size, int flags, uint32_t position) {
    return -ENOTSUP;
}
static int uf_getxattr(const char *path, const char *name, char *value, size_t size, uint32_t position) {
    return -ENOTSUP;
}
static int uf_listxattr(const char *path, char *list, size_t size) {
    return -ENOTSUP;
}
#endif

static struct fuse_operations uf_ops = {
    .getattr  = uf_getattr,
    .readdir  = uf_readdir,
    .open     = uf_open,
    .create   = uf_create,
    .release  = uf_release,
    .read     = uf_read,
    .write    = uf_write,
    .mkdir    = uf_mkdir,
    .rmdir    = uf_rmdir,
    .unlink   = uf_unlink,
    .rename   = uf_rename,
    .truncate = uf_truncate,
    .chmod    = uf_chmod,
    .chown    = uf_chown,
#if defined(__APPLE__)
    .setxattr = uf_setxattr,
    .getxattr = uf_getxattr,
    .listxattr = uf_listxattr,
#endif
};


#define SPN_FL512S_PAGE_SIZE    512
#define SPN_FL512S_SECTOR_SIZE  (256 * 1024)
#define SPN_FL512S_SIZE         (64 * 1024 * 1024)

#define FL512S_TOTAL_BLOCKS     (SPN_FL512S_SIZE / SPN_FL512S_SECTOR_SIZE)
#define FL512S_TOTAL_BLOCKS     (SPN_FL512S_SIZE / SPN_FL512S_SECTOR_SIZE)
#define FL512S_PAGES_PER_BLOCK  (SPN_FL512S_SECTOR_SIZE / SPN_FL512S_PAGE_SIZE)
#define FL512S_PAGE_SPARE_SIZE  16
#define FL512S_PAGE_DATA_SIZE   (SPN_FL512S_PAGE_SIZE - FL512S_PAGE_SPARE_SIZE)
#define FL512S_BLOCK_ST_OFFSET  4


static void setup_storage(uffs_Device *dev)
{
    dev->attr = femu_GetStorage();

	dev->attr->total_blocks = FL512S_TOTAL_BLOCKS;
	dev->attr->page_data_size = FL512S_PAGE_DATA_SIZE;
	dev->attr->pages_per_block = FL512S_PAGES_PER_BLOCK;
    dev->attr->spare_size = FL512S_PAGE_SPARE_SIZE;
	dev->attr->block_status_offs = FL512S_BLOCK_ST_OFFSET;
	dev->attr->ecc_opt = UFFS_ECC_SOFT;
	dev->attr->layout_opt = UFFS_LAYOUT_UFFS;
}

static void setup_emu_private(uffs_Device *dev, const char *emu_filename)
{
    dev->_private = (void *)emu_filename;
}

static void setup_device(uffs_Device *dev, const char *img_file)
{
    dev->Init = femu_InitDevice;
    dev->Release = femu_ReleaseDevice;

    setup_storage(dev);
    setup_emu_private(dev, img_file);
}

static int init_uffs(const char *mount_point, const char *img_file) {

    struct uffs_MountTableEntrySt *mtbl = malloc(sizeof(*mtbl));

    if (!mtbl) {
        fprintf(stderr, "Error allocating mount table for mount_point %s\n", mount_point);
        exit(1);
    }

    uffs_Device *g_dev = malloc(sizeof(*g_dev));

    if (!g_dev) {
        fprintf(stderr, "Error allocating uffs_device for mount_point %s\n", mount_point);
        exit(1);
    }

	mtbl->mount = strdup(mount_point);
	mtbl->start_block = 0;
	mtbl->end_block = FL512S_TOTAL_BLOCKS - 1;
    mtbl->dev = g_dev;

    setup_device(mtbl->dev, img_file);

    size_t bufsz = UFFS_STATIC_BUFF_SIZE(FL512S_PAGES_PER_BLOCK, SPN_FL512S_PAGE_SIZE, FL512S_TOTAL_BLOCKS);
    void *buf = malloc(bufsz);
    uffs_MemSetupStaticAllocator(&mtbl->dev->mem, buf, bufsz);

    if (uffs_RegisterMountTable(mtbl) != 0) {
        fprintf(stderr, "Error registering the mount table for mount point %s\n", mount_point);
        exit(1);
    }

    uffs_Mount(mtbl->mount);

    const u8 *p = mtbl->dev->attr->data_layout;
    while(p && *p != 0xFF) { fprintf(stderr, "%d+%d ", p[0], p[1]); p+=2; }
    p = mtbl->dev->attr->ecc_layout;
    while(p && *p != 0xFF) { fprintf(stderr, "%d+%d ", p[0], p[1]); p+=2; }

    uffs_InitFileSystemObjects();
    return 0;
}

int main(int argc, char *argv[]) {
    uffs_SetupDebugOutput();

    if (argc < 4) {
        fprintf(stderr, "Usage: %s <n_images> <flash_0.img>...<flash_n-1.img> <mountpoint> [fuse opts]\n", argv[0]);
        return 1;
    }

    size_t num_images = strtoull(argv[1], NULL, 10);

    if (num_images == 0) {
        fprintf(stderr, "Invalid num images, it must be grater than zero!\n");
        return 1;
    }

    argv[1] = argv[0];
    argv++;
    argc--;

    char tmp_name[32] = {0};

    for (size_t i = 0; i < num_images; i++) {
        const char *imgfile = argv[1];

        if (snprintf(tmp_name, sizeof(tmp_name), "/flash%zu/", i) >= sizeof(tmp_name)) {
            fprintf(stderr, "Too many images passed!\n");
            return 1;
        }

        if (init_uffs(tmp_name, imgfile)) {
            fprintf(stderr, "Failed to mount UFFS image\n");
            return 1;
        }

        argv[1] = argv[0];
        argv++;
        argc--;
    }

    return fuse_main(argc, argv, &uf_ops, NULL);
}

