#include "main.h"

/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int do_check (const char *path)
{
	char *basename_path;
	basename_path = strrchr(path, '/');
	if (basename_path == NULL) {
		debugf("this should not happen %s", path);
		return -ENOENT;
	}
	basename_path++;
	if (strlen(basename_path) > 255) {
		debugf("basename exceeds 255 characters %s",path);
		return -ENAMETOOLONG;
	}
	return 0;
}

int do_check_split (const char *path, char **dirname, char **basename)
{
	char *tmp;
	char *cpath = strdup(path);
	tmp = strrchr(cpath, '/');
	if (tmp == NULL) {
		debugf("this should not happen %s", path);
		free(cpath);
		return -ENOENT;
	}
	*tmp='\0';
	tmp++;
	if (strlen(tmp) > 255) {
		debugf("basename exceeds 255 characters %s",path);
		free(cpath);
		return -ENAMETOOLONG;
	}
	*dirname = cpath;
	*basename = tmp;
	return 0;
}

void free_split (char *dirname, char *basename)
{
	free(dirname);
}

/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


static inline dev_t old_decode_dev (__u16 val)
{
	return makedev((val >> 8) & 255, val & 255);
}

static inline dev_t new_decode_dev (__u32 dev)
{
	unsigned major = (dev & 0xfff00) >> 8;
	unsigned minor = (dev & 0xff) | ((dev >> 12) & 0xfff00);
	return makedev(major, minor);
}

void do_fillstatbuf (ext2_filsys e2fs, ext2_ino_t ino, struct ext2_inode *inode, struct stat *st)
{
	debugf("enter");
	memset(st, 0, sizeof(*st));
	/* XXX workaround
	 * should be unique and != existing devices */
	st->st_dev = (dev_t) ((long) e2fs);
	st->st_ino = ino;
	st->st_mode = inode->i_mode;
	st->st_nlink = inode->i_links_count;
	st->st_uid = ext2_read_uid(inode);
	st->st_gid = ext2_read_gid(inode);
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		if (inode->i_block[0]) {
			st->st_rdev = old_decode_dev(ext2fs_le32_to_cpu(inode->i_block[0]));
		} else {
			st->st_rdev = new_decode_dev(ext2fs_le32_to_cpu(inode->i_block[1]));
		}
	} else {
		st->st_rdev = 0;
	}
	st->st_size = EXT2_I_SIZE(inode);
	st->st_blksize = EXT2_BLOCK_SIZE(e2fs->super);
	st->st_blocks = inode->i_blocks;
	st->st_atime = inode->i_atime;
	st->st_mtime = inode->i_mtime;
	st->st_ctime = inode->i_ctime;
#if __FreeBSD__ == 10
	st->st_gen = inode->i_generation;
#endif
	debugf("leave");
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

static int release_blocks_proc (ext2_filsys fs, blk_t *blocknr, int blockcnt, void *private)
{
	blk_t block;

	debugf("enter");

	block = *blocknr;
	ext2fs_block_alloc_stats(fs, block, -1);

	debugf("leave");
#ifdef CLEAN_UNUSED_BLOCKS
	*blocknr = 0;
	return BLOCK_CHANGED;
#else
	return 0;
#endif
}

int do_killfilebyinode (ext2_filsys e2fs, ext2_ino_t ino, struct ext2_inode *inode)
{
	errcode_t rc;
	char scratchbuf[3*e2fs->blocksize];

	debugf("enter");

	inode->i_links_count = 0;
	inode->i_dtime = time(NULL);

	rc = ext2fs_write_inode(e2fs, ino, inode);
	if (rc) {
		debugf("ext2fs_write_inode(e2fs, ino, inode); failed");
		return -EIO;
	}

	if (ext2fs_inode_has_valid_blocks(inode)) {
		debugf("start block delete for %d", ino);
#ifdef CLEAN_UNUSED_BLOCKS
		ext2fs_block_iterate(e2fs, ino, BLOCK_FLAG_DEPTH_TRAVERSE, scratchbuf, release_blocks_proc, NULL);
#else
		ext2fs_block_iterate(e2fs, ino, 0, scratchbuf, release_blocks_proc, NULL);
#endif
	}

	ext2fs_inode_alloc_stats2(e2fs, ino, -1, LINUX_S_ISDIR(inode->i_mode));

	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define VOLNAME_SIZE_MAX 16

int do_probe (struct supFs_data *opts)
{
	errcode_t rc;
	ext2_filsys e2fs;

	debugf_main("enter");

	rc = ext2fs_open(opts->device, EXT2_FLAG_RW, 0, 0, unix_io_manager, &e2fs);
	if (rc) {
		debugf_main("Error while trying to open %s (rc=%d)", opts->device, rc);
		return -1;
	}
#if 0
	rc = ext2fs_read_bitmaps(e2fs);
	if (rc) {
		debugf_main("Error while reading bitmaps (rc=%d)", rc);
		ext2fs_close(e2fs);
		return -2;
	}
#endif
	if (e2fs->super != NULL) {
		opts->volname = (char *) malloc(sizeof(char) * (VOLNAME_SIZE_MAX + 1));
		if (opts->volname != NULL) {
			memset(opts->volname, 0, sizeof(char) * (VOLNAME_SIZE_MAX + 1));
			strncpy(opts->volname, e2fs->super->s_volume_name, VOLNAME_SIZE_MAX);
			opts->volname[VOLNAME_SIZE_MAX] = '\0';
		}
	}
	ext2fs_close(e2fs);

	debugf_main("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

int do_readinode (ext2_filsys e2fs, const char *path, ext2_ino_t *ino, struct ext2_inode *inode)
{
	errcode_t rc;
	rc = ext2fs_namei(e2fs, EXT2_ROOT_INO, EXT2_ROOT_INO, path, ino);
	if (rc) {
		debugf("ext2fs_namei(e2fs, EXT2_ROOT_INO, EXT2_ROOT_INO, %s, ino); failed", path);
		return -ENOENT;
	}
	rc = ext2fs_read_inode(e2fs, *ino, inode);
	if (rc) {
		debugf("ext2fs_read_inode(e2fs, *ino, inode); failed");
		return -EIO;
	}
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

int do_writeinode (ext2_filsys e2fs, ext2_ino_t ino, struct ext2_inode *inode)
{
	int rt;
	errcode_t rc;
	if (inode->i_links_count < 1) {
		rt = do_killfilebyinode(e2fs, ino, inode);
		if (rt) {
			debugf("do_killfilebyinode(e2fs, ino, inode); failed");
			return rt;
		}
	} else {
		rc = ext2fs_write_inode(e2fs, ino, inode);
		if (rc) {
			debugf("ext2fs_read_inode(e2fs, *ino, inode); failed");
			return -EIO;
		}
	}
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int op_access (const char *path, int mask)
{
	int rt;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s, mask = 0%o", path, mask);
	
	rt = do_check(path);
	if (rt != 0) {
		debugf("do_check(%s); failed", path);
		return rt;
	}

	if ((mask & W_OK) && !(e2fs->flags & EXT2_FLAG_RW)) {
		return -EACCES;
	}
	
	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int op_chmod (const char *path, mode_t mode)
{
	int rt;
	int mask;
	time_t tm;
	ext2_ino_t ino;
	struct ext2_inode inode;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s 0%o", path, mode);

	rt = do_check(path);
	if (rt != 0) {
		debugf("do_check(%s); failed", path);
		return rt;
	}

	rt = do_readinode(e2fs, path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &vnode); failed", path);
		return rt;
	}

	tm = e2fs->now ? e2fs->now : time(NULL);
	mask = S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID | S_ISVTX;
	inode.i_mode = (inode.i_mode & ~mask) | (mode & mask);
	inode.i_ctime = tm;

	rt = do_writeinode(e2fs, ino, &inode);
	if (rt) {
		debugf("do_writeinode(e2fs, ino, &inode); failed");
		return -EIO;
	}

	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int op_chown (const char *path, uid_t uid, gid_t gid)
{
	int rt;
	ext2_ino_t ino;
	struct ext2_inode inode;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s", path);
	
	rt = do_check(path);
	if (rt != 0) {
		debugf("do_check(%s); failed", path);
		return rt;
	}

	rt = do_readinode(e2fs, path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &vnode); failed", path);
		return rt;
	}
	
	if (uid != -1) {
		ext2_write_uid(&inode, uid);
	}
	if (gid != -1) {
		ext2_write_gid(&inode, gid);
	}

	rt = do_writeinode(e2fs, ino, &inode);
	if (rt) {
		debugf("do_writeinode(e2fs, ino, &inode); failed");
		return -EIO;
	}

	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int do_modetoext2lag (mode_t mode)
{
	if (S_ISREG(mode)) {
		return EXT2_FT_REG_FILE;
	} else if (S_ISDIR(mode)) {
		return EXT2_FT_DIR;
	} else if (S_ISCHR(mode)) {
		return EXT2_FT_CHRDEV;
	} else if (S_ISBLK(mode)) {
		return EXT2_FT_BLKDEV;
	} else if (S_ISFIFO(mode)) {
		return EXT2_FT_FIFO;
	} else if (S_ISSOCK(mode)) {
		return EXT2_FT_SOCK;
	} else if (S_ISLNK(mode)) {
		return EXT2_FT_SYMLINK;
	}
	return EXT2_FT_UNKNOWN;
}

static inline int old_valid_dev(dev_t dev)
{
	return major(dev) < 256 && minor(dev) < 256;
}

static inline __u16 old_encode_dev(dev_t dev)
{
	return (major(dev) << 8) | minor(dev);
}

static inline __u32 new_encode_dev(dev_t dev)
{
	unsigned major = major(dev);
	unsigned minor = minor(dev);
	return (minor & 0xff) | (major << 8) | ((minor & ~0xff) << 12);
}

int do_create (ext2_filsys e2fs, const char *path, mode_t mode, dev_t dev, const char *fastsymlink)
{
	int rt;
	time_t tm;
	errcode_t rc;

	char *p_path;
	char *r_path;

	ext2_ino_t ino;
	struct ext2_inode inode;
	ext2_ino_t n_ino;

	struct fuse_context *ctx;

	debugf("enter");
	debugf("path = %s, mode: 0%o", path, mode);

	rt=do_check_split(path, &p_path, &r_path);

	debugf("parent: %s, child: %s", p_path, r_path);

	rt = do_readinode(e2fs, p_path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &inode); failed", p_path);
		free_split(p_path, r_path);
		return rt;
	}

	rc = ext2fs_new_inode(e2fs, ino, mode, 0, &n_ino);
	if (rc) {
		debugf("ext2fs_new_inode(ep.fs, ino, mode, 0, &n_ino); failed");
		free_split(p_path, r_path);
		return -ENOMEM;
	}

	do {
		debugf("calling ext2fs_link(e2fs, %d, %s, %d, %d);", ino, r_path, n_ino, do_modetoext2lag(mode));
		rc = ext2fs_link(e2fs, ino, r_path, n_ino, do_modetoext2lag(mode));
		if (rc == EXT2_ET_DIR_NO_SPACE) {
			debugf("calling ext2fs_expand_dir(e2fs, &d)", ino);
			if (ext2fs_expand_dir(e2fs, ino)) {
				debugf("error while expanding directory %s (%d)", p_path, ino);
				free_split(p_path, r_path);
				return -ENOSPC;
			}
		}
	} while (rc == EXT2_ET_DIR_NO_SPACE);
	if (rc) {
		debugf("ext2fs_link(e2fs, %d, %s, %d, %d); failed", ino, r_path, n_ino, do_modetoext2lag(mode));
		free_split(p_path, r_path);
		return -EIO;
	}

	if (ext2fs_test_inode_bitmap(e2fs->inode_map, n_ino)) {
		debugf("inode already set");
	}

	ext2fs_inode_alloc_stats2(e2fs, n_ino, +1, 0);
	memset(&inode, 0, sizeof(inode));
	tm = e2fs->now ? e2fs->now : time(NULL);
	inode.i_mode = mode;
	inode.i_atime = inode.i_ctime = inode.i_mtime = tm;
	inode.i_links_count = 1;
	inode.i_size = 0;
	ctx = fuse_get_context();
	if (ctx) {
		ext2_write_uid(&inode, ctx->uid);
		ext2_write_gid(&inode, ctx->gid);
	}
	if (e2fs->super->s_feature_incompat &
	    EXT3_FEATURE_INCOMPAT_EXTENTS) {
		int i;
		struct ext3_extent_header *eh;

		eh = (struct ext3_extent_header *) &inode.i_block[0];
		eh->eh_depth = 0;
		eh->eh_entries = 0;
		eh->eh_magic = ext2fs_cpu_to_le16(EXT3_EXT_MAGIC);
		i = (sizeof(inode.i_block) - sizeof(*eh)) /
			sizeof(struct ext3_extent);
		eh->eh_max = ext2fs_cpu_to_le16(i);
		inode.i_flags |= EXT4_EXTENTS_FL;
	}

	if (S_ISCHR(mode) || S_ISBLK(mode)) {
		if (old_valid_dev(dev))
			inode.i_block[0]= ext2fs_cpu_to_le32(old_encode_dev(dev));
		else
			inode.i_block[1]= ext2fs_cpu_to_le32(new_encode_dev(dev));
	}

	if (S_ISLNK(mode) && fastsymlink != NULL) {
		inode.i_size = strlen(fastsymlink);
		strncpy((char *)&(inode.i_block[0]),fastsymlink,
				(EXT2_N_BLOCKS * sizeof(inode.i_block[0])));
	}

	rc = ext2fs_write_new_inode(e2fs, n_ino, &inode);
	if (rc) {
		debugf("ext2fs_write_new_inode(e2fs, n_ino, &inode);");
		free_split(p_path, r_path);
		return -EIO;
	}

	/* update parent dir */
	rt = do_readinode(e2fs, p_path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &inode); dailed", p_path);
		free_split(p_path, r_path);
		return -EIO;
	}
	inode.i_ctime = inode.i_mtime = tm;
	rc = do_writeinode(e2fs, ino, &inode);
	if (rc) {
		debugf("do_writeinode(e2fs, ino, &inode); failed");
		free_split(p_path, r_path);
		return -EIO;
	}

	free_split(p_path, r_path);

	debugf("leave");
	return 0;
}

int op_create (const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int rt;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s, mode: 0%o", path, mode);

	if (op_open(path, fi) == 0) {
		debugf("leave");
		return 0;
	}

	rt = do_create(e2fs, path, mode, 0, NULL);
	if (rt != 0) {
		return rt;
	}

	if (op_open(path, fi)) {
		debugf("op_open(path, fi); failed");
		return -EIO;
	}

	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


void op_destroy (void *userdata)
{
	errcode_t rc;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	rc = ext2fs_close(e2fs);
	if (rc) {
		debugf("Error while trying to close ext2 filesystem");
	}
	e2fs = NULL;
	debugf("leave");
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int op_fgetattr (const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
	int rt;
	ext2_ino_t ino;
	struct ext2_inode inode;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s", path);
	
	rt = do_check(path);
	if (rt != 0) {
		debugf("do_check(%s); failed", path);
		return rt;
	}

	rt = do_readinode(e2fs, path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &vnode); failed", path);
		return rt;
	}
	do_fillstatbuf(e2fs, ino, &inode, stbuf);

	debugf("path: %s, size: %d", path, stbuf->st_size);
	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int op_flush (const char *path, struct fuse_file_info *fi)
{
	errcode_t rc;
	ext2_file_t efile = EXT2FS_FILE(fi->fh);

	debugf("enter");
	debugf("path = %s (%p)", path, efile);
	
	if (efile == NULL) {
		return -ENOENT;
	}
	
	rc = ext2fs_file_flush(efile);
	if (rc) {
		return -EIO;
	}
	
	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int op_fsync (const char *path, int datasync, struct fuse_file_info *fi)
{
	errcode_t rc;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s (%p)", path, fi);
	
	rc = ext2fs_flush(e2fs);
	if (rc) {
		return -EIO;
	}

	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int op_getattr (const char *path, struct stat *stbuf)
{
	int rt;
	ext2_ino_t ino;
	struct ext2_inode inode;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s", path);

	rt = do_check(path);
	if (rt != 0) {
		debugf("do_check(%s); failed", path);
		return rt;
	}

	rt = do_readinode(e2fs, path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &vnode); failed", path);
		return rt;
	}
	do_fillstatbuf(e2fs, ino, &inode, stbuf);

	debugf("path: %s, size: %d", path, stbuf->st_size);
	debugf("leave");
	return 0;
}
/**
 * @file
 * @brief EXT2 getxattr support
 *
 * @date 14.01.2015
 * @author Alexander Kalmuk
 */


static int do_getxattr(ext2_filsys e2fs, struct ext2_inode *node, const char *name,
		char *value, size_t size);

int op_getxattr(const char *path, const char *name, char *value, size_t size) {
	int rt;
	ext2_ino_t ino;
	struct ext2_inode inode;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s", path);
	debugf("path = %s, %s, %p, %d", path, name, value, size);

	rt = do_check(path);
	if (rt != 0) {
		debugf("do_check(%s); failed", path);
		return rt;
	}

	rt = do_readinode(e2fs, path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &inode); failed", path);
		return rt;
	}

	rt = do_getxattr(e2fs, &inode, name, value, size);
	if (rt < 0) {
		debugf("do_getxattr(e2fs, inode, %s, value, %d); failed", name, size);
		return rt;
	}

	debugf("leave");
	return rt;
}

/**
 * Name is of format namespace:attribute. This function return namespace as @p name_index
 * and attribute as @p attr_name
 *
 * TODO support trusted, system and security attributes
 */
static int parse_name(const char *name, int *name_index, char **attr_name) {
	char namespace[16];
	char *attr_name_str;

	memcpy(namespace, name, sizeof namespace);

	attr_name_str = strchr(namespace, '.');
	if (!attr_name) {
		return -ENOTSUP;
	} else {
		*attr_name_str = 0;
		*attr_name = ++attr_name_str;
	}

	if (!strcmp(namespace, "user")) {
		*name_index = 1;
		return 0;
	}

	return -ENOTSUP;
}

static int do_getxattr(ext2_filsys e2fs, struct ext2_inode *node, const char *name,
		char *value, size_t size) {
	char *buf, *attr_start;
	struct ext2_ext_attr_entry *entry;
	char *entry_name, *value_name;
	int name_index;
	int res;

	res = parse_name(name, &name_index, &value_name);
	if (res < 0) {
		return res;
	}

	buf = malloc(e2fs->blocksize);
	if (!buf) {
		return -ENOMEM;
	}
	ext2fs_read_ext_attr(e2fs, node->i_file_acl, buf);

	attr_start = buf + sizeof(struct ext2_ext_attr_header);
	entry = (struct ext2_ext_attr_entry *) attr_start;
	res = -ENODATA;

	while (!EXT2_EXT_IS_LAST_ENTRY(entry)) {
		entry_name = (char *)entry + sizeof(struct ext2_ext_attr_entry);

		if (name_index == entry->e_name_index &&
				entry->e_name_len == strlen(value_name)) {
			if (!strncmp(entry_name, value_name, entry->e_name_len)) {
				if (size > 0) {
					memcpy(value, buf + entry->e_value_offs, entry->e_value_size);
				}
				res = entry->e_value_size;
				break;
			}
		}
		entry = EXT2_EXT_ATTR_NEXT(entry);
	}

	free(buf);
	return res;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


void * op_init (struct fuse_conn_info *conn)
{
	errcode_t rc;
	struct fuse_context *cntx=fuse_get_context();
	struct supFs_data *e2data=cntx->private_data;

	debugf("enter %s", e2data->device);

	rc = ext2fs_open(e2data->device, 
			(e2data->readonly) ? 0 : EXT2_FLAG_RW,
			0, 0, unix_io_manager, &e2data->e2fs);
	if (rc) {
		debugf("Error while trying to open %s", e2data->device);
		exit(1);
	}
#if 1
	if (e2data->readonly != 1)
#endif
	rc = ext2fs_read_bitmaps(e2data->e2fs);
	if (rc) {
		debugf("Error while reading bitmaps");
		ext2fs_close(e2data->e2fs);
		exit(1);
	}
	debugf("FileSystem %s", (e2data->e2fs->flags & EXT2_FLAG_RW) ? "Read&Write" : "ReadOnly");

	debugf("leave");

	return e2data;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int op_link (const char *source, const char *dest)
{
	int rc;
	char *p_path;
	char *r_path;

	ext2_ino_t s_ino;
	ext2_ino_t d_ino;
	struct ext2_inode s_inode;
	struct ext2_inode d_inode;
	ext2_filsys e2fs = current_ext2fs();

	debugf("source: %s, dest: %s", source, dest);

	rc = do_check(source);
	if (rc != 0) {
		debugf("do_check(%s); failed", source);
		return rc;
	}

	rc = do_check_split(dest, &p_path, &r_path);
	if (rc != 0) {
		debugf("do_check(%s); failed", dest);
		return rc;
	}

	debugf("parent: %s, child: %s", p_path, r_path);

	rc = do_readinode(e2fs, p_path, &d_ino, &d_inode);
	if (rc) {
		debugf("do_readinode(%s, &ino, &inode); failed", p_path);
		free_split(p_path, r_path);
		return rc;
	}

	rc = do_readinode(e2fs, source, &s_ino, &s_inode);
	if (rc) {
		debugf("do_readinode(%s, &s_ino, &s_inode); failed", p_path);
		free_split(p_path, r_path);
		return rc;
	}

	do {
		debugf("calling ext2fs_link(e2fs, %d, %s, %d, %d);", d_ino, r_path, s_ino, do_modetoext2lag(s_inode.i_mode));
		rc = ext2fs_link(e2fs, d_ino, r_path, s_ino, do_modetoext2lag(s_inode.i_mode));
		if (rc == EXT2_ET_DIR_NO_SPACE) {
			debugf("calling ext2fs_expand_dir(e2fs, &d)", d_ino);
			if (ext2fs_expand_dir(e2fs, d_ino)) {
				debugf("error while expanding directory %s (%d)", p_path, d_ino);
				free_split(p_path, r_path);
				return -ENOSPC;
			}
		}
	} while (rc == EXT2_ET_DIR_NO_SPACE);
	if (rc) {
		debugf("ext2fs_link(e2fs, %d, %s, %d, %d); failed", d_ino, r_path, s_ino, do_modetoext2lag(s_inode.i_mode));
		free_split(p_path, r_path);
		return -EIO;
	}

	d_inode.i_mtime = d_inode.i_ctime = s_inode.i_ctime = e2fs->now ? e2fs->now : time(NULL);
	s_inode.i_links_count += 1;

	rc = do_writeinode(e2fs, s_ino, &s_inode);
	if (rc) {
		debugf("do_writeinode(e2fs, s_ino, &s_inode); failed");
		free_split(p_path, r_path);
		return -EIO;
	}

	rc = do_writeinode(e2fs, d_ino, &d_inode);
	if (rc) {
		debugf("do_writeinode(e2fs, d_ino, &d_inode); failed");
		free_split(p_path, r_path);
		return -EIO;
	}

	free_split(p_path, r_path);
	debugf("done");

	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int op_mkdir (const char *path, mode_t mode)
{
	int rt;
	time_t tm;
	errcode_t rc;

	char *p_path;
	char *r_path;

	ext2_ino_t ino;
	struct ext2_inode inode;

	struct fuse_context *ctx;

	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s, mode: 0%o, dir:0%o", path, mode, LINUX_S_IFDIR);

	rt = do_check_split(path, &p_path ,&r_path);
	if (rt != 0) {
		debugf("do_check(%s); failed", path);
		return rt;
	}

	debugf("parent: %s, child: %s, pathmax: %d", p_path, r_path, PATH_MAX);

	rt = do_readinode(e2fs, p_path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &inode); failed", p_path);
		free_split(p_path, r_path);
		return rt;
	}

	do {
		debugf("calling ext2fs_mkdir(e2fs, %d, 0, %s);", ino, r_path);
		rc = ext2fs_mkdir(e2fs, ino, 0, r_path);
		if (rc == EXT2_ET_DIR_NO_SPACE) {
			debugf("calling ext2fs_expand_dir(e2fs, &d)", ino);
			if (ext2fs_expand_dir(e2fs, ino)) {
				debugf("error while expanding directory %s (%d)", p_path, ino);
				free_split(p_path, r_path);
				return -ENOSPC;
			}
		}
	} while (rc == EXT2_ET_DIR_NO_SPACE);
	if (rc) {
		debugf("ext2fs_mkdir(e2fs, %d, 0, %s); failed (%d)", ino, r_path, rc);
		debugf("e2fs: %p, e2fs->inode_map: %p", e2fs, e2fs->inode_map);
		free_split(p_path, r_path);
		return -EIO;
	}

	rt = do_readinode(e2fs, path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &inode); failed", path);
		free_split(p_path, r_path);
		return -EIO;
	}
	tm = e2fs->now ? e2fs->now : time(NULL);
	inode.i_mode = LINUX_S_IFDIR | mode;
	inode.i_ctime = inode.i_atime = inode.i_mtime = tm;
	ctx = fuse_get_context();
	if (ctx) {
		ext2_write_uid(&inode, ctx->uid);
		ext2_write_gid(&inode, ctx->gid);
	}
	rc = do_writeinode(e2fs, ino, &inode);
	if (rc) {
		debugf("do_writeinode(e2fs, ino, &inode); failed");
		free_split(p_path, r_path);
		return -EIO;
	}

	/* update parent dir */
	rt = do_readinode(e2fs, p_path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &inode); dailed", p_path);
		free_split(p_path, r_path);
		return -EIO;
	}
	inode.i_ctime = inode.i_mtime = tm;
	rc = do_writeinode(e2fs, ino, &inode);
	if (rc) {
		debugf("do_writeinode(e2fs, ino, &inode); failed");
		free_split(p_path, r_path);
		return -EIO;
	}

	free_split(p_path, r_path);

	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int op_mknod (const char *path, mode_t mode, dev_t dev)
{
	int rt;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s 0%o", path, mode);

	rt = do_create(e2fs, path, mode, dev, NULL);

	debugf("leave");
	return rt;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


ext2_file_t do_open (ext2_filsys e2fs, const char *path, int flags)
{
	int rt;
	errcode_t rc;
	ext2_ino_t ino;
	ext2_file_t efile;
	struct ext2_inode inode;
	struct fuse_context *cntx = fuse_get_context();
	struct supFs_data *e2data = cntx->private_data;

	debugf("enter");
	debugf("path = %s", path);

	rt = do_check(path);
	if (rt != 0) {
		debugf("do_check(%s); failed", path);
		return NULL;
	}

	rt = do_readinode(e2fs, path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &inode); failed", path);
		return NULL;
	}

	rc = ext2fs_file_open2(
			e2fs,
			ino,
			&inode,
			(((flags & O_ACCMODE) != 0) ? EXT2_FILE_WRITE : 0) | EXT2_FILE_SHARED_INODE, 
			&efile);
	if (rc) {
		return NULL;
	}

	if (e2data->readonly == 0) {
		inode.i_atime = e2fs->now ? e2fs->now : time(NULL);
		rt = do_writeinode(e2fs, ino, &inode);
		if (rt) {
			debugf("do_writeinode(%s, &ino, &inode); failed", path);
			return NULL;
		}
	}

	debugf("leave");
	return efile;
}

int op_open (const char *path, struct fuse_file_info *fi)
{
	ext2_file_t efile;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s", path);

	efile = do_open(e2fs, path, fi->flags);
	if (efile == NULL) {
		debugf("do_open(%s); failed", path);
		return -ENOENT;
	}
	fi->fh = (uint64_t) efile;

	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int op_read (const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	__u64 pos;
	errcode_t rc;
	unsigned int bytes;
	ext2_file_t efile = EXT2FS_FILE(fi->fh);
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s", path);

	efile = do_open(e2fs, path, O_RDONLY);
	rc = ext2fs_file_llseek(efile, offset, SEEK_SET, &pos);
	if (rc) {
		do_release(efile);
		return -EINVAL;
	}

	rc = ext2fs_file_read(efile, buf, size, &bytes);
	if (rc) {
		do_release(efile);
		return -EIO;
	}
	do_release(efile);

	debugf("leave");
	return bytes;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


struct dir_walk_data {
	char *buf;
	fuse_fill_dir_t filler;
};

#define _USE_DIR_ITERATE2
#ifdef _USE_DIR_ITERATE2
static int walk_dir2 (ext2_ino_t dir, int   entry, struct ext2_dir_entry *dirent, int offset, int blocksize, char *buf, void *vpsid)
{
	int res;
	int len;
	struct stat st;
	unsigned char type;
	if (dirent->name_len <= 0) {
		return 0;
	}
	struct dir_walk_data *psid=(struct dir_walk_data *)vpsid;
	memset(&st, 0, sizeof(st));

	len = dirent->name_len & 0xff;
	dirent->name[len] = 0; // bug wraparound

	switch  (dirent->name_len >> 8) {
		case EXT2_FT_UNKNOWN: 	type = DT_UNKNOWN;	break;
		case EXT2_FT_REG_FILE:	type = DT_REG;		break;
		case EXT2_FT_DIR:	type = DT_DIR;		break;
		case EXT2_FT_CHRDEV:	type = DT_CHR;		break;
		case EXT2_FT_BLKDEV:	type = DT_BLK;		break;
		case EXT2_FT_FIFO:	type = DT_FIFO;		break;
		case EXT2_FT_SOCK:	type = DT_SOCK;		break;
		case EXT2_FT_SYMLINK:	type = DT_LNK;		break;
		default:		type = DT_UNKNOWN;	break;
	}
	st.st_ino = dirent->inode;
	st.st_mode = type << 12;
	debugf("%s %d %d %d", dirent->name, dirent->name_len & 0xff, dirent->name_len >> 8, type);
	res = psid->filler(psid->buf, dirent->name, &st, 0);
	if (res != 0) {
		return BLOCK_ABORT;
	}
	return 0;
}
#else
static int walk_dir (struct ext2_dir_entry *de, int offset, int blocksize, char *buf, void *priv_data)
{
	int ret;
	size_t flen;
	char *fname;
	struct dir_walk_data *b = priv_data;

	debugf("enter");

	flen = de->name_len & 0xff;
	fname = (char *) malloc(sizeof(char) * (flen + 1));
	if (fname == NULL) {
		debugf("s = (char *) malloc(sizeof(char) * (%d + 1)); failed", flen);
		return -ENOMEM;
	}
	snprintf(fname, flen + 1, "%s", de->name);
	debugf("b->filler(b->buf, %s, NULL, 0);", fname);
	ret = b->filler(b->buf, fname, NULL, 0);
	free(fname);
	
	debugf("leave");
	return ret;
}
#endif

int op_readdir (const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	int rt;
	errcode_t rc;
	ext2_ino_t ino;
	struct ext2_inode inode;
	struct dir_walk_data dwd={
		.buf = buf,
		.filler = filler};
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s", path);
	
	rt = do_readinode(e2fs, path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &inode); failed", path);
		return rt;
	}

#ifdef _USE_DIR_ITERATE2
	rc = ext2fs_dir_iterate2(e2fs,ino, DIRENT_FLAG_INCLUDE_EMPTY, NULL, walk_dir2, &dwd);
#else
	rc = ext2fs_dir_iterate(e2fs, ino, 0, NULL, walk_dir, &dwd);
#endif

	if (rc) {
		debugf("Error while trying to ext2fs_dir_iterate %s", path);
		return -EIO;
	}

	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int op_readlink (const char *path, char *buf, size_t size)
{
	int rt;
	size_t s;
	errcode_t rc;
	ext2_ino_t ino;
	char *b = NULL;
	char *pathname;
	struct ext2_inode inode;
	ext2_filsys e2fs = current_ext2fs();
	
	debugf("enter");
	debugf("path = %s", path);

	rt = do_readinode(e2fs, path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &inode); failed", path);
		return rt;
	}
	
	if (!LINUX_S_ISLNK(inode.i_mode)) {
		debugf("%s is not a link", path);
		return -EINVAL;
	}
	
	if (ext2fs_inode_data_blocks(e2fs, &inode)) {
		rc = ext2fs_get_mem(EXT2_BLOCK_SIZE(e2fs->super), &b);
		if (rc) {
			debugf("ext2fs_get_mem(EXT2_BLOCK_SIZE(e2fs->super), &b); failed");
			return -ENOMEM;
		}
		rc = io_channel_read_blk(e2fs->io, inode.i_block[0], 1, b);
		if (rc) {
			ext2fs_free_mem(&b);
			debugf("io_channel_read_blk(e2fs->io, inode.i_block[0], 1, b); failed");
			return -EIO;
		}
		pathname = b;
	} else {
		pathname = (char *) &(inode.i_block[0]);
	}
	
	debugf("pathname: %s", pathname);
	
	s = (size < strlen(pathname) + 1) ? size : strlen(pathname) + 1;
	snprintf(buf, s, "%s", pathname);
	
	if (b) {
		ext2fs_free_mem(&b);
	}

	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int do_release (ext2_file_t efile)
{
	errcode_t rc;

	debugf("enter");
	debugf("path = (%p)", efile);

	if (efile == NULL) {
		return -ENOENT;
	}
	rc = ext2fs_file_close(efile);
	if (rc) {
		return -EIO;
	}

	debugf("leave");
	return 0;
}

int op_release (const char *path, struct fuse_file_info *fi)
{
	int rt;
	ext2_file_t efile = (ext2_file_t) (unsigned long) fi->fh;

	debugf("enter");
	debugf("path = %s (%p)", path, efile);
	rt = do_release(efile);
	if (rt != 0) {
		debugf("do_release() failed");
		return rt;
	}

	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


static int fix_dotdot_proc (ext2_ino_t dir EXT2FS_ATTR((unused)),
		int entry EXT2FS_ATTR((unused)),
		struct ext2_dir_entry *dirent,
		int offset EXT2FS_ATTR((unused)),
		int blocksize EXT2FS_ATTR((unused)),
		char *buf EXT2FS_ATTR((unused)), void *private)
{
	ext2_ino_t *p_dotdot = (ext2_ino_t *) private;

	debugf("enter");
	debugf("walking on: %s", dirent->name);

	if ((dirent->name_len & 0xFF) == 2 && strncmp(dirent->name, "..", 2) == 0) {
		dirent->inode = *p_dotdot;

		debugf("leave (found '..')");
		return DIRENT_ABORT | DIRENT_CHANGED;
	} else {
		debugf("leave");
		return 0;
	}
}

static int do_fix_dotdot (ext2_filsys e2fs, ext2_ino_t ino, ext2_ino_t dotdot)
{
	errcode_t rc;

	debugf("enter");
	rc = ext2fs_dir_iterate2(e2fs, ino, DIRENT_FLAG_INCLUDE_EMPTY, 
			0, fix_dotdot_proc, &dotdot);
	if (rc) {
		debugf("while iterating over directory");
		return -EIO;
	}
	debugf("leave");
	return 0;
}

int op_rename (const char *source, const char *dest)
{
	int rt;
	errcode_t rc;

	char *p_src;
	char *r_src;
	char *p_dest;
	char *r_dest;

	ext2_ino_t src_ino;
	ext2_ino_t dest_ino;
	ext2_ino_t d_src_ino;
	ext2_ino_t d_dest_ino;
	struct ext2_inode src_inode;
	struct ext2_inode dest_inode;
	struct ext2_inode d_src_inode;
	struct ext2_inode d_dest_inode;
	ext2_filsys e2fs = current_ext2fs();

	debugf("source: %s, dest: %s", source, dest);

	rt = do_check_split(source, &p_src, &r_src);
	if (rt != 0) {
		debugf("do_check(%s); failed", source);
		return rt;
	}

	debugf("src_parent: %s, src_child: %s", p_src, r_src);

	rt = do_check_split(dest, &p_dest, &r_dest);
	if (rt != 0) {
		debugf("do_check(%s); failed", dest);
		return rt;
	}

	debugf("dest_parent: %s, dest_child: %s", p_dest, r_dest);

	rt = do_readinode(e2fs, p_src, &d_src_ino, &d_src_inode);
	if (rt != 0) {
		debugf("do_readinode(%s, &d_src_ino, &d_src_inode); failed", p_src);
		goto out;
	}

	rt = do_readinode(e2fs, p_dest, &d_dest_ino, &d_dest_inode);
	if (rt != 0) {
		debugf("do_readinode(%s, &d_dest_ino, &d_dest_inode); failed", p_dest);
		goto out;
	}

	rt = do_readinode(e2fs, source, &src_ino, &src_inode);
	if (rt != 0) {
		debugf("do_readinode(%s, &src_ino, &src_inode); failed", p_dest);
		goto out;
	}

	rt = do_readinode(e2fs, dest, &dest_ino, &dest_inode);
	if (rt != 0 && rt != -ENOENT) {
		debugf("do_readinode(%s, &dest_ino, &dest_inode); failed", dest);
		goto out;
	}

	/* If oldpath  and  newpath are existing hard links referring to the same
	 * file, then rename() does nothing, and returns a success status.
	 */
	if (rt == 0 && src_ino == dest_ino) {
		goto out;
	}

	/* EINVAL:
	 *   The  new  pathname  contained a path prefix of the old, this should be checked by fuse
	 */
	if (rt == 0) {
		if (LINUX_S_ISDIR(dest_inode.i_mode)) {
			/* EISDIR:
			 *   newpath  is  an  existing directory, but oldpath is not a directory.
			 */
			if (!(LINUX_S_ISDIR(src_inode.i_mode))) {
				debugf("newpath is dir && oldpath is not a dir -> EISDIR");
				rt = -EISDIR;
				goto out;
			}
			/* ENOTEMPTY:
			 *   newpath is a non-empty  directory
			 */
			rt = do_check_empty_dir(e2fs, dest_ino);
			if (rt != 0) {
				debugf("do_check_empty_dir dest %s failed",dest);
				goto out;
			}
		}
		/* ENOTDIR:
		 *   oldpath  is a directory, and newpath exists but is not a directory
		 */
		if (LINUX_S_ISDIR(src_inode.i_mode) &&
		    !(LINUX_S_ISDIR(dest_inode.i_mode))) {
			debugf("oldpath is dir && newpath is not a dir -> ENOTDIR");
			rt = -ENOTDIR;
			goto out;
		}

		/* Step 1: if destination exists: delete it */
		if (LINUX_S_ISDIR(dest_inode.i_mode)) {
			rc = op_rmdir(dest);
		} else {
			rc = op_unlink(dest);
		}
		if (rc) {
			debugf("do_writeinode(e2fs, ino, inode); failed");
			goto out;
		}
		rt = do_readinode(e2fs, p_dest, &d_dest_ino, &d_dest_inode);
		if (rt != 0) {
			debugf("do_readinode(%s, &d_dest_ino, &d_dest_inode); failed", p_dest);
			goto out;
		}
	}
  	
	/* Step 2: add the link */
	do {
		debugf("calling ext2fs_link(e2fs, %d, %s, %d, %d);", d_dest_ino, r_dest, src_ino, do_modetoext2lag(src_inode.i_mode));
		rc = ext2fs_link(e2fs, d_dest_ino, r_dest, src_ino, do_modetoext2lag(src_inode.i_mode));
		if (rc == EXT2_ET_DIR_NO_SPACE) {
			debugf("calling ext2fs_expand_dir(e2fs, &d)", src_ino);
			if (ext2fs_expand_dir(e2fs, d_dest_ino)) {
				debugf("error while expanding directory %s (%d)", p_dest, d_dest_ino);
				rt = -ENOSPC;
				goto out;
			}
			/* ext2fs_expand_dir changes d_dest_inode */
			rt = do_readinode(e2fs, p_dest, &d_dest_ino, &d_dest_inode);
			if (rt != 0) {
				debugf("do_readinode(%s, &d_dest_ino, &d_dest_inode); failed", p_dest);
				goto out;
			}
		}
	} while (rc == EXT2_ET_DIR_NO_SPACE);
	if (rc != 0) {
		debugf("ext2fs_link(e2fs, %d, %s, %d, %d); failed", d_dest_ino, r_dest, src_ino, do_modetoext2lag(src_inode.i_mode));
		rt = -EIO;
		goto out;
	}

	/* Special case: if moving dir across different parents fix counters and '..' */
	if (LINUX_S_ISDIR(src_inode.i_mode) && d_src_ino != d_dest_ino) {
		d_dest_inode.i_links_count++;
		if (d_src_inode.i_links_count > 1) {
			d_src_inode.i_links_count--;
		}
		rc = do_writeinode(e2fs, d_src_ino, &d_src_inode);
		if (rc != 0) {
			debugf("do_writeinode(e2fs, src_ino, &src_inode); failed");
			rt = -EIO;
			goto out;
		}
		rt = do_fix_dotdot(e2fs, src_ino, d_dest_ino);
		if (rt != 0) {
			debugf("do_fix_dotdot failed");
			goto out;
		}
	}

	/* utimes and inodes update */
	d_dest_inode.i_mtime = d_dest_inode.i_ctime = src_inode.i_ctime = e2fs->now ? e2fs->now : time(NULL);
	rt = do_writeinode(e2fs, d_dest_ino, &d_dest_inode);
	if (rt != 0) {
		debugf("do_writeinode(e2fs, d_dest_ino, &d_dest_inode); failed");
		goto out;
	}
	rt = do_writeinode(e2fs, src_ino, &src_inode);
	if (rt != 0) {
		debugf("do_writeinode(e2fs, src_ino, &src_inode); failed");
		goto out;
	}
	debugf("done");

	/* Step 3: delete the source */

	rc = ext2fs_unlink(e2fs, d_src_ino, r_src, src_ino, 0);
	if (rc) {
		debugf("while unlinking src ino %d", (int) src_ino);
		rt = -EIO;
		goto out;
	}

out:	free_split(p_src, r_src);
	free_split(p_dest, r_dest);
	return rt;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


struct rmdir_st {
	ext2_ino_t parent;
	int empty;
};

static int rmdir_proc (ext2_ino_t dir EXT2FS_ATTR((unused)),
		       int entry EXT2FS_ATTR((unused)),
		       struct ext2_dir_entry *dirent,
		       int offset EXT2FS_ATTR((unused)),
		       int blocksize EXT2FS_ATTR((unused)),
		       char *buf EXT2FS_ATTR((unused)), void *private)
{
	int *p_empty= (int *) private;

	debugf("enter");
	debugf("walking on: %s", dirent->name);

	if (dirent->inode == 0 ||
			(((dirent->name_len & 0xFF) == 1) && (dirent->name[0] == '.')) ||
			(((dirent->name_len & 0xFF) == 2) && (dirent->name[0] == '.') && 
			 (dirent->name[1] == '.'))) {
		debugf("leave");
		return 0;
	}
	*p_empty = 0;
	debugf("leave (not empty)");
	return 0;
}

int do_check_empty_dir(ext2_filsys e2fs, ext2_ino_t ino)
{
	errcode_t rc;
	int empty = 1;

	rc = ext2fs_dir_iterate2(e2fs, ino, 0, 0, rmdir_proc, &empty);
	if (rc) {
		debugf("while iterating over directory");
		return -EIO;
	}

	if (empty == 0) {
		debugf("directory not empty");
		return -ENOTEMPTY;
	}

	return 0;
}

int op_rmdir (const char *path)
{
	int rt;
	errcode_t rc;

	char *p_path;
	char *r_path;

	ext2_ino_t p_ino;
	struct ext2_inode p_inode;
	ext2_ino_t r_ino;
	struct ext2_inode r_inode;
	
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s", path);

	rt = do_check_split(path, &p_path, &r_path);
	if (rt != 0) {
		debugf("do_check_split: failed");
		return rt;
	}

	debugf("parent: %s, child: %s", p_path, r_path);
	
	rt = do_readinode(e2fs, p_path, &p_ino, &p_inode);
	if (rt) {
		debugf("do_readinode(%s, &p_ino, &p_inode); failed", p_path);
		free_split(p_path, r_path);
		return rt;
	}
	rt = do_readinode(e2fs, path, &r_ino, &r_inode);
	if (rt) {
		debugf("do_readinode(%s, &r_ino, &r_inode); failed", path);
		free_split(p_path, r_path);
		return rt;
		
	}
	if (!LINUX_S_ISDIR(r_inode.i_mode)) {
		debugf("%s is not a directory", path);
		free_split(p_path, r_path);
		return -ENOTDIR;
	}
	if (r_ino == EXT2_ROOT_INO) {
		debugf("root dir cannot be removed", path);
		free_split(p_path, r_path);
		return -EIO;
	}
	
	rt = do_check_empty_dir(e2fs, r_ino);
	if (rt) {
		debugf("do_check_empty_dir filed");
		free_split(p_path, r_path);
		return rt;
	}

	rc = ext2fs_unlink(e2fs, p_ino, r_path, r_ino, 0);
	if (rc) {
		debugf("while unlinking ino %d", (int) r_ino);
		free_split(p_path, r_path);
		return -EIO;
	}

	rt = do_killfilebyinode(e2fs, r_ino, &r_inode);
	if (rt) {
		debugf("do_killfilebyinode(r_ino, &r_inode); failed");
		free_split(p_path, r_path);
		return rt;
	}

	rt = do_readinode(e2fs, p_path, &p_ino, &p_inode);
	if (rt) {
		debugf("do_readinode(p_path, &p_ino, &p_inode); failed");
		free_split(p_path, r_path);
		return rt;
	}
	if (p_inode.i_links_count > 1) {
		p_inode.i_links_count--;
	}
	p_inode.i_mtime = e2fs->now ? e2fs->now : time(NULL);
	p_inode.i_ctime = e2fs->now ? e2fs->now : time(NULL);
	rc = do_writeinode(e2fs, p_ino, &p_inode);
	if (rc) {
		debugf("do_writeinode(e2fs, ino, inode); failed");
		free_split(p_path, r_path);
		return -EIO;
	}

	free_split(p_path, r_path);

	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


static int test_root (unsigned int a, unsigned int b)
{
	while (1) {
		if (a < b) {
			return 0;
		}
		if (a == b) {
			return 1;
		}
		if (a % b) {
			return 0;
		}
		a = a / b;
	}
}

static int ext2_group_spare (int group)
{
	if (group <= 1) {
		return 1;
	}
	return (test_root(group, 3) || test_root(group, 5) || test_root(group, 7));
}

static int ext2_bg_has_super (ext2_filsys e2fs, int group)
{
	if (EXT2_HAS_RO_COMPAT_FEATURE(e2fs->super, EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER) &&
	    !ext2_group_spare(group)) {
		return 0;
	}
	return 1;
}

static int ext2_bg_num_gdb (ext2_filsys e2fs, int group)
{
	if (EXT2_HAS_RO_COMPAT_FEATURE(e2fs->super, EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER) &&
	    !ext2_group_spare(group)) {
		return 0;
	}
	return 1;
}

#define EXT2_BLOCKS_COUNT(s) ((s)->s_blocks_count | ((__u64) (s)->s_blocks_count_hi << 32))
#define EXT2_RBLOCKS_COUNT(s) ((s)->s_r_blocks_count | ((__u64) (s)->s_r_blocks_count_hi << 32))
#define EXT2_FBLOCKS_COUNT(s) ((s)->s_free_blocks_count | ((__u64) (s)->s_free_blocks_hi << 32))

int op_statfs (const char *path, struct statvfs *buf)
{
	unsigned long long i;
	unsigned long long s_gdb_count = 0;
	unsigned long long s_groups_count = 0;
	unsigned long long s_itb_per_group = 0;
	unsigned long long s_overhead_last = 0;
	unsigned long long s_inodes_per_block = 0;

	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");

	memset(buf, 0, sizeof(struct statvfs));

	if (e2fs->super->s_default_mount_opts & EXT2_MOUNT_MINIX_DF) {
		s_overhead_last = 0;
	} else {
		s_overhead_last = e2fs->super->s_first_data_block;
		s_groups_count = ((EXT2_BLOCKS_COUNT(e2fs->super) - e2fs->super->s_first_data_block - 1) / e2fs->super->s_blocks_per_group) + 1;
		s_gdb_count = (s_groups_count + EXT2_DESC_PER_BLOCK(e2fs->super) - 1) / EXT2_DESC_PER_BLOCK(e2fs->super);
		for (i = 0; i < s_groups_count; i++) {
			s_overhead_last += ext2_bg_has_super(e2fs, i) + ((ext2_bg_num_gdb(e2fs, i) == 0) ? 0 : s_gdb_count);
		}
		s_inodes_per_block = EXT2_BLOCK_SIZE(e2fs->super) / EXT2_INODE_SIZE(e2fs->super);
		s_itb_per_group = e2fs->super->s_inodes_per_group / s_inodes_per_block;
		s_overhead_last += (s_groups_count * (2 +  s_itb_per_group));
	}
	buf->f_bsize = EXT2_BLOCK_SIZE(e2fs->super);
	buf->f_frsize = EXT2_FRAG_SIZE(e2fs->super);
	buf->f_blocks = EXT2_BLOCKS_COUNT(e2fs->super) - s_overhead_last;
	buf->f_bfree = EXT2_FBLOCKS_COUNT(e2fs->super);
	if (EXT2_FBLOCKS_COUNT(e2fs->super) < EXT2_RBLOCKS_COUNT(e2fs->super)) {
		buf->f_bavail = 0;
	} else {
		buf->f_bavail = EXT2_FBLOCKS_COUNT(e2fs->super) - EXT2_RBLOCKS_COUNT(e2fs->super);
	}
	buf->f_files = e2fs->super->s_inodes_count;
	buf->f_ffree = e2fs->super->s_free_inodes_count;
	buf->f_favail = e2fs->super->s_free_inodes_count;
	buf->f_namemax = EXT2_NAME_LEN;

	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int op_symlink (const char *sourcename, const char *destname)
{
	int rt;
	size_t wr;
	ext2_file_t efile;
	ext2_filsys e2fs = current_ext2fs();
	int sourcelen = strlen(sourcename);

	debugf("enter");
	debugf("source: %s, dest: %s", sourcename, destname);

	/* a short symlink is stored in the inode (recycling the i_block array) */
	if (sourcelen < (EXT2_N_BLOCKS * sizeof(__u32))) {
		rt = do_create(e2fs, destname, LINUX_S_IFLNK | 0777, 0, sourcename);
		if (rt != 0) {
			debugf("do_create(%s, LINUX_S_IFLNK | 0777, FAST); failed", destname);
			return rt;
		}
	} else {
		rt = do_create(e2fs, destname, LINUX_S_IFLNK | 0777, 0, NULL);
		if (rt != 0) {
			debugf("do_create(%s, LINUX_S_IFLNK | 0777); failed", destname);
			return rt;
		}
		efile = do_open(e2fs, destname, O_WRONLY);
		if (efile == NULL) {
			debugf("do_open(%s); failed", destname);
			return -EIO;
		}
		wr = do_write(efile, sourcename, sourcelen, 0);
		if (wr != strlen(sourcename)) {
			debugf("do_write(efile, %s, %d, 0); failed", sourcename, strlen(sourcename) + 1);
			return -EIO;
		}
		rt = do_release(efile);
		if (rt != 0) {
			debugf("do_release(efile); failed");
			return rt;
		}
	}
	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */


int op_truncate (const char *path, off_t length)
{
	int rt;
	errcode_t rc;
	ext2_ino_t ino;
	struct ext2_inode inode;
	ext2_file_t efile;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s", path);

	rt = do_check(path);
	if (rt != 0) {
		debugf("do_check(%s); failed", path);
		return rt;
	}
	efile = do_open(e2fs, path, O_WRONLY);
	if (efile == NULL) {
		debugf("do_open(%s); failed", path);
		return -ENOENT;
	}

	rc = ext2fs_file_set_size2(efile, length);
	if (rc) {
		do_release(efile);
		debugf("ext2fs_file_set_size(efile, %d); failed", length);
		if (rc == EXT2_ET_FILE_TOO_BIG) {
			return -EFBIG;
		}
		return -EIO;
	}

	rt = do_readinode(e2fs, path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &vnode); failed", path);
		do_release(efile);
		return rt;
	}
	inode.i_ctime = e2fs->now ? e2fs->now : time(NULL);
	rt = do_writeinode(e2fs, ino, &inode);
	if (rt) {
		debugf("do_writeinode(e2fs, ino, &inode); failed");
		do_release(efile);
		return -EIO;
	}

	rt = do_release(efile);
	if (rt != 0) {
		debugf("do_release(efile); failed");
		return rt;
	}

	debugf("leave");
	return 0;
}

int op_ftruncate (const char *path, off_t length, struct fuse_file_info *fi)
{
	(void) fi;
	return op_truncate(path, length);
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */



int op_unlink (const char *path)
{
	int rt;
	errcode_t rc;

	char *p_path;
	char *r_path;

	ext2_ino_t p_ino;
	ext2_ino_t r_ino;
	struct ext2_inode p_inode;
	struct ext2_inode r_inode;

	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s", path);

	rt = do_check(path);
	if (rt != 0) {
		debugf("do_check(%s); failed", path);
		return rt;
	}

	rt = do_check_split(path, &p_path, &r_path);
	if (rt != 0) {
		debugf("do_check_split: failed");
		return rt;
	}

	debugf("parent: %s, child: %s", p_path, r_path);

	rt = do_readinode(e2fs, p_path, &p_ino, &p_inode);
	if (rt) {
		debugf("do_readinode(%s, &p_ino, &p_inode); failed", path);
		free_split(p_path, r_path);
		return rt;
	}
	rt = do_readinode(e2fs, path, &r_ino, &r_inode);
	if (rt) {
		debugf("do_readinode(%s, &r_ino, &r_inode); failed", path);
		free_split(p_path, r_path);
		return rt;
	}

	if (LINUX_S_ISDIR(r_inode.i_mode)) {
		debugf("%s is a directory", path);
		free_split(p_path, r_path);
		return -EISDIR;
	}

	rc = ext2fs_unlink(e2fs, p_ino, r_path, r_ino, 0);
	if (rc) {
		debugf("ext2fs_unlink(e2fs, %d, %s, %d, 0); failed", p_ino, r_path, r_ino);
		free_split(p_path, r_path);
		return -EIO;
	}

	p_inode.i_ctime = p_inode.i_mtime = e2fs->now ? e2fs->now : time(NULL);
	rt = do_writeinode(e2fs, p_ino, &p_inode);
	if (rt) {
		debugf("do_writeinode(e2fs, p_ino, &p_inode); failed");
		free_split(p_path, r_path);
		return -EIO;
	}

	if (r_inode.i_links_count > 0) {
		r_inode.i_links_count -= 1;
	}
	r_inode.i_ctime = e2fs->now ? e2fs->now : time(NULL);
	rc = do_writeinode(e2fs, r_ino, &r_inode);
	if (rc) {
		debugf("do_writeinode(e2fs, &r_ino, &r_inode); failed");
		free_split(p_path, r_path);
		return -EIO;
	}

	free_split(p_path, r_path);
	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

int op_utimens (const char *path, const struct timespec tv[2])
{
	int rt;
	ext2_ino_t ino;
	struct ext2_inode inode;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s", path);
	
	rt = do_check(path);
	if (rt != 0) {
		debugf("do_check(%s); failed", path);
		return rt;
	}

	rt = do_readinode(e2fs, path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ino, &vnode); failed", path);
		return rt;
	}
	
	inode.i_atime = tv[0].tv_sec;
	inode.i_mtime = tv[0].tv_sec;

	rt = do_writeinode(e2fs, ino, &inode);
	if (rt) {
		debugf("do_writeinode(e2fs, ino, &inode); failed");
		return -EIO;
	}

	debugf("leave");
	return 0;
}
/**
 * Copyright (c) 2008-2015 Alper Akcan <alper.akcan@gmail.com>
 * Copyright (c) 2009 Renzo Davoli <renzo@cs.unibo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the fuse-ext2
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

size_t do_write (ext2_file_t efile, const char *buf, size_t size, off_t offset)
{
	int rt;
	const char *tmp;
	unsigned int wr;
	unsigned long long npos;
	unsigned long long fsize;

	debugf("enter");

	rt = ext2fs_file_get_lsize(efile, &fsize);
	if (rt != 0) {
		debugf("ext2fs_file_get_lsize(efile, &fsize); failed");
		return rt;
	}
	if (offset + size > fsize) {
		rt = ext2fs_file_set_size2(efile, offset + size);
		if (rt) {
			debugf("extfs_file_set_size(efile, %lld); failed", offset + size);
			return rt;
		}
	}

	rt = ext2fs_file_llseek(efile, offset, SEEK_SET, &npos);
	if (rt) {
		debugf("ext2fs_file_lseek(efile, %lld, SEEK_SET, &npos); failed", offset);
		return rt;
	}

	for (rt = 0, wr = 0, tmp = buf; size > 0 && rt == 0; size -= wr, tmp += wr) {
		rt = ext2fs_file_write(efile, tmp, size, &wr);
		debugf("rt: %d, size: %u, written: %u", rt, size, wr);
	}
	if (rt) {
		debugf("ext2fs_file_write(edile, tmp, size, &wr); failed");
		return rt;
	}

	rt = ext2fs_file_flush(efile);
	if (rt) {
		debugf("ext2_file_flush(efile); failed");
		return rt;
	}

	debugf("leave");
	return wr;
}

int op_write (const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	size_t rt;
	ext2_file_t efile = EXT2FS_FILE(fi->fh);
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s", path);

	efile = do_open(e2fs, path, O_WRONLY);
	rt = do_write(efile, buf, size, offset);
	do_release(efile);

	debugf("leave");
	return rt;
}
