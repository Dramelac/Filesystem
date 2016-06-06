#ifndef MAIN_H_
#define MAIN_H_

#define FUSE_USE_VERSION 26

#include "../config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>


#include <unistd.h>

#include <fuse.h>
#include <ext2fs/ext2fs.h>

#include "SupFS_log.h"

#if !defined(FUSE_VERSION) || (FUSE_VERSION < 26)
#error "***********************************************************"
#error "*                                                         *"
#error "*     Compilation requires at least FUSE version 2.6.0!   *"
#error "*                                                         *"
#error "***********************************************************"
#endif

/* extra definitions not yet included in ext2fs.h */
#define EXT2_FILE_SHARED_INODE 0x8000
errcode_t ext2fs_file_close2(ext2_file_t file, void (*close_callback) (struct ext2_inode *inode, int flags));

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define EXT2FS_FILE(efile) ((void *) (unsigned long) (efile))
/* max timeout to flush bitmaps, to reduce inconsistencies */
#define FLUSH_BITMAPS_TIMEOUT 10


struct supFS_state{
    char *rootDir;//device
    unsigned char debug;
    unsigned char silent;
    unsigned char force;
    unsigned char readonly;
    time_t last_flush;
    char *mnt_point;
    char *options;
    ext2_filsys e2fs;

};

struct dir_walk_data {
    char *buf;
    fuse_fill_dir_t filler;
};

#define SUPFS_DATA ((struct supFS_state *) fuse_get_context()->private_data)


static inline ext2_filsys current_ext2fs(void)
{
    log_info("A", "");
    struct fuse_context *mycontext=fuse_get_context();
    struct supFS_state *e2data=mycontext->private_data;
    time_t now=time(NULL);
    if ((now - e2data->last_flush) > FLUSH_BITMAPS_TIMEOUT) {
        ext2fs_write_bitmaps(e2data->e2fs);
        e2data->last_flush=now;
    }
    log_info("B", "");
    return (ext2_filsys) e2data->e2fs;
}

static inline uid_t ext2_read_uid(struct ext2_inode *inode)
{
    return ((uid_t)inode->osd2.linux2.l_i_uid_high << 16) | inode->i_uid;
}

static inline void ext2_write_uid(struct ext2_inode *inode, uid_t uid)
{
    inode->i_uid = uid & 0xffff;
    inode->osd2.linux2.l_i_uid_high = (uid >> 16) & 0xffff;
}

static inline gid_t ext2_read_gid(struct ext2_inode *inode)
{
    return ((gid_t)inode->osd2.linux2.l_i_gid_high << 16) | inode->i_gid;
}

static inline void ext2_write_gid(struct ext2_inode *inode, gid_t gid)
{
    inode->i_gid = gid & 0xffff;
    inode->osd2.linux2.l_i_gid_high = (gid >> 16) & 0xffff;
}


int do_check(const char *path);
int do_readinode (ext2_filsys e2fs, const char *path, ext2_ino_t *ino, struct ext2_inode *inode);
void do_fillstatbuf (ext2_filsys e2fs, ext2_ino_t ino, struct ext2_inode *inode, struct stat *st);

int do_killfilebyinode (ext2_filsys e2fs, ext2_ino_t ino, struct ext2_inode *inode);
int do_writeinode (ext2_filsys e2fs, ext2_ino_t ino, struct ext2_inode *inode);
ext2_file_t do_open (ext2_filsys e2fs, const char *path, int flags);

int fs_release (ext2_file_t efile);
int walk_dir(ext2_ino_t dir, int   entry, struct ext2_dir_entry *dirent, int offset, int blocksize, char *buf, void *vpsid);



#endif