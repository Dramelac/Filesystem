//
// Created by mathieu on 27/05/16.
//

#include "main.h"

int do_check(const char *path)
{
    char *basename_path;
    basename_path = strrchr(path, '/');
    if (basename_path == NULL) {
        log_error("do check fatal error");
        return -ENOENT;
    }
    basename_path++;
    if (strlen(basename_path) > 255) {
        log_error("do check path too long");
        return -ENAMETOOLONG;
    }
    return 0;
}

int do_readinode (ext2_filsys e2fs, const char *path, ext2_ino_t *ino, struct ext2_inode *inode)
{
    errcode_t rc;
    rc = ext2fs_namei(e2fs, EXT2_ROOT_INO, EXT2_ROOT_INO, path, ino);
    if (rc) {
        log_info("ext2fs_namei(e2fs, EXT2_ROOT_INO, EXT2_ROOT_INO, %s, ino); failed / ", path);
        return -ENOENT;
    }
    rc = ext2fs_read_inode(e2fs, *ino, inode);
    if (rc) {
        log_error("ext2fs_read_inode(e2fs, *ino, inode); failed");
        return -EIO;
    }
    return 0;
}

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
}

int fs_release (ext2_file_t efile)
{
    errcode_t rc;

    if (efile == NULL) {
        return -ENOENT;
    }
    rc = ext2fs_file_close(efile);
    if (rc) {
        return -EIO;
    }

    return 0;
}

static int release_blocks_proc (ext2_filsys fs, blk_t *blocknr, int blockcnt, void *private)
{
    blk_t block;

    block = *blocknr;
    ext2fs_block_alloc_stats(fs, block, -1);

    return 0;
}

int do_killfilebyinode (ext2_filsys e2fs, ext2_ino_t ino, struct ext2_inode *inode)
{
    errcode_t rc;
    char scratchbuf[3*e2fs->blocksize];

    inode->i_links_count = 0;
    inode->i_dtime = time(NULL);

    rc = ext2fs_write_inode(e2fs, ino, inode);
    if (rc) {
        log_error("ext2fs_write_inode(e2fs, ino, inode); failed");
        return -EIO;
    }

    if (ext2fs_inode_has_valid_blocks(inode)) {
        log_info("start block delete for %d", "");

        ext2fs_block_iterate(e2fs, ino, 0, scratchbuf, release_blocks_proc, NULL);

    }

    ext2fs_inode_alloc_stats2(e2fs, ino, -1, LINUX_S_ISDIR(inode->i_mode));

    return 0;
}

int do_writeinode (ext2_filsys e2fs, ext2_ino_t ino, struct ext2_inode *inode)
{
    int rt;
    errcode_t rc;
    if (inode->i_links_count < 1) {
        rt = do_killfilebyinode(e2fs, ino, inode);
        if (rt) {
            log_error("do_killfilebyinode(e2fs, ino, inode); failed");
            return rt;
        }
    } else {
        rc = ext2fs_write_inode(e2fs, ino, inode);
        if (rc) {
            log_error("ext2fs_read_inode(e2fs, *ino, inode); failed");
            return -EIO;
        }
    }
    return 0;
}

ext2_file_t do_open (ext2_filsys e2fs, const char *path, int flags)
{
    int rt;
    errcode_t rc;
    ext2_ino_t ino;
    ext2_file_t efile;
    struct ext2_inode inode;
    struct fuse_context *cntx = fuse_get_context();
    struct supFS_state *e2data = cntx->private_data;

    rt = do_check(path);
    if (rt != 0) {
        log_error("do_open");
        return NULL;
    }

    rt = do_readinode(e2fs, path, &ino, &inode);
    if (rt) {
        log_error("do_open");
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
            log_error("write inode");
            return NULL;
        }
    }

    return efile;

}

int walk_dir(ext2_ino_t dir, int   entry, struct ext2_dir_entry *dirent, int offset, int blocksize, char *buf, void *vpsid)
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
    res = psid->filler(psid->buf, dirent->name, &st, 0);
    if (res != 0) {
        return BLOCK_ABORT;
    }
    return 0;
}
