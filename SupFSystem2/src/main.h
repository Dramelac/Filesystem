#ifndef FUSEEXT2_H_
#define FUSEEXT2_H_

#define FUSE_USE_VERSION 27

#include <../config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>

#include <fuse.h>
#include <ext2fs/ext2fs.h>
#include "SupFS_log.h"

/* extra definitions not yet included in ext2fs.h */
#define EXT2_FILE_SHARED_INODE 0x8000

#define EXT2FS_FILE(efile) ((void *) (unsigned long) (efile))

struct supFs_data {
    time_t last_flush;
    char *mnt_point;
    char *device;
    ext2_filsys e2fs;
};

static ext2_filsys getCurrent_e2fs();

#if ENABLE_DEBUG

static inline void debug_printf (const char *function, char *file, int line, const char *fmt, ...)
{
	va_list args;
	struct fuse_context *mycontext=fuse_get_context();
	struct supFs_data *e2data=mycontext->private_data;
	if (e2data && (e2data->debug == 0 || e2data->silent == 1)) {
		return;
	}
	printf("%s: ", PACKAGE);
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	printf(" [%s (%s:%d)]\n", function, file, line);
}

#define debugf(a...) { \
	debug_printf(__FUNCTION__, __FILE__, __LINE__, a); \
}

static inline void debug_main_printf (const char *function, char *file, int line, const char *fmt, ...)
{
	va_list args;
	printf("%s: ", PACKAGE);
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	printf(" [%s (%s:%d)]\n", function, file, line);
}

#define debugf_main(a...) { \
	debug_main_printf(__FUNCTION__, __FILE__, __LINE__, a); \
}

#else /* ENABLE_DEBUG */

#define debugf(a...) do { } while(0)
#define debugf_main(a...) do { } while(0)

#endif /* ENABLE_DEBUG */

void * initE2fs(struct fuse_conn_info *conn);

void destroy(void *userdata);

/* helper functions */

int check (const char *path);

int checkToDir(const char *path, char **dirname, char **basename);

void fillstatbuffer(ext2_filsys e2fs, ext2_ino_t ext2Ino, struct ext2_inode *inode, struct stat *statBuff);

int readNode (ext2_filsys e2fs, const char *path, ext2_ino_t *ino, struct ext2_inode *inode);

int writeNode(ext2_filsys e2fs, ext2_ino_t ino, struct ext2_inode *inode);

int changeFileInode(ext2_filsys e2fs, ext2_ino_t ext2Ino, struct ext2_inode *inode);

/* read support */

int check_access(const char *path, int mask);

int supFS_getattr (const char *path, struct stat *stbuf);

ext2_file_t process_open (ext2_filsys e2fs, const char *path, int flags);

int supFS_open(const char *path, struct fuse_file_info *fi);

int supFS_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

int supFS_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);

int releaseFile(ext2_file_t efile);

int op_release (const char *path, struct fuse_file_info *fi);

/* write support */

int modeToExt2Flag(mode_t mode);

int createNode(ext2_filsys e2fs, const char *path, mode_t mode, dev_t dev, const char *fastsymlink);

int op_create (const char *path, mode_t mode, struct fuse_file_info *fi);

int op_flush (const char *path, struct fuse_file_info *fi);

int op_mkdir (const char *path, mode_t mode);

int do_check_empty_dir(ext2_filsys e2fs, ext2_ino_t ino);

int op_rmdir (const char *path);

int op_unlink (const char *path);

size_t do_write (ext2_file_t efile, const char *buf, size_t size, off_t offset);

int op_write (const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

int op_mknod (const char *path, mode_t mode, dev_t dev);

int op_truncate(const char *path, off_t length);

int op_rename (const char *source, const char *dest);

#endif /* FUSEEXT2_H_ */
