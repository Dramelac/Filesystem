#include "main.h"

ext2_filsys static current_ext2fs(void) {

    struct fuse_context *fuseGetContext = fuse_get_context();
    struct supFs_data *fsData = fuseGetContext->private_data;

    time_t now = time(NULL);
    /* check flush for redundancy */
    if ((now - fsData->last_flush) > 10) {
        ext2fs_write_bitmaps(fsData->e2fs);
        fsData->last_flush=now;
    }

    return fsData->e2fs;
}

int check(const char *path)
{
	char *basename_path;
    // shrink path
	basename_path = strrchr(path, '/');
    // check null error
	if (basename_path == NULL) {
		return -ENOENT;
	}
	basename_path++;
    // check size error
	if (strlen(basename_path) > 255) {
		return -ENAMETOOLONG;
	}
    // check valid
	return 0;
}

int checkToDir(const char *path, char **dirname, char **basename)
{
	char *tmpPath;
	char *cpPath = strdup(path);
	tmpPath = strrchr(cpPath, '/');
	if (tmpPath == NULL) {
		free(cpPath);
		return -ENOENT;
	}
	*tmpPath='\0';
	tmpPath++;
	if (strlen(tmpPath) > 255) {
		free(cpPath);
		return -ENAMETOOLONG;
	}
	*dirname = cpPath;
	*basename = tmpPath;
	return 0;
}

void fillstatbuffer(ext2_filsys e2fs, ext2_ino_t ext2Ino, struct ext2_inode *inode, struct stat *statBuff)
{
	memset(statBuff, 0, sizeof(*statBuff));

	statBuff->st_dev = (dev_t) e2fs;
	statBuff->st_ino = ext2Ino;
	statBuff->st_mode = inode->i_mode;
	statBuff->st_nlink = inode->i_links_count;
	statBuff->st_uid = inode->i_uid;
	statBuff->st_gid = inode->i_gid;
	statBuff->st_size = EXT2_I_SIZE(inode);
	statBuff->st_blksize = EXT2_BLOCK_SIZE(e2fs->super);
	statBuff->st_blocks = inode->i_blocks;
	statBuff->st_atime = inode->i_atime;
	statBuff->st_mtime = inode->i_mtime;
	statBuff->st_ctime = inode->i_ctime;

}

static int release_blocks_proc (ext2_filsys fs, blk_t *blocknr, int blockcnt, void *private)
{
	blk_t block;
	block = *blocknr;
	ext2fs_block_alloc_stats(fs, block, -1);

	return 0;
}

int changeFileInode(ext2_filsys e2fs, ext2_ino_t ext2Ino, struct ext2_inode *inode)
{
	errcode_t rc;
	char scratchbuf[3*e2fs->blocksize];

	inode->i_links_count = 0;
	inode->i_dtime = time(NULL);

	rc = ext2fs_write_inode(e2fs, ext2Ino, inode);
	if (rc) {

		debugf("ext2fs_write_inode(e2fs, ext2Ino, ext2Inode); failed");
		return -EIO;

	}

	if (ext2fs_inode_has_valid_blocks(inode)) {
		ext2fs_block_iterate(e2fs, ext2Ino, 0, scratchbuf, release_blocks_proc, NULL);
	}
	ext2fs_inode_alloc_stats2(e2fs, ext2Ino, -1, LINUX_S_ISDIR(inode->i_mode));

	return 0;
}


int readNode (ext2_filsys e2fs, const char *path, ext2_ino_t *ext2Ino, struct ext2_inode *ext2Inode)
{
    if (ext2fs_namei(e2fs, EXT2_ROOT_INO, EXT2_ROOT_INO, path, ext2Ino)) {
        char logError[1000] =  "ext2fs-namei process FAILED :";
        strcat(logError,path);
        log_error(logError);
		return -ENOENT;
	}
	if (ext2fs_read_inode(e2fs, *ext2Ino, ext2Inode)) {
		return -EIO;
	}
	return 0;
}


int writeNode(ext2_filsys e2fs, ext2_ino_t ino, struct ext2_inode *inode)
{
	int returnValue = 0;
	if (inode->i_links_count < 1) {

		returnValue = changeFileInode(e2fs, ino, inode);
		if (returnValue) {
            return returnValue;

        }
	} else if (ext2fs_write_inode(e2fs, ino, inode)) {
        return -EIO;
	}
	return returnValue;
}


int check_access(const char *path, int mask)
{
	int rt;
	ext2_filsys e2fs = current_ext2fs();
	
	rt = check(path);
	if (rt != 0) {
		return rt;
	}

	if ((mask & W_OK) && !(e2fs->flags & EXT2_FLAG_RW)) {
		return -EACCES;
	}
	return 0;
}


int modeToExt2Flag(mode_t mode)
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

int supFS_create(ext2_filsys e2fs, const char *path, mode_t mode, dev_t dev, const char *fastsymlink)
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

	checkToDir(path, &p_path, &r_path);
	rt = readNode(e2fs, p_path, &ino, &inode);

	if (rt) {

        free(p_path);
		return rt;
	}

	rc = ext2fs_new_inode(e2fs, ino, mode, 0, &n_ino);
	if (rc) {
        free(p_path);
		return -ENOMEM;
	}

	do {
		rc = ext2fs_link(e2fs, ino, r_path, n_ino, modeToExt2Flag(mode));
		if (rc == EXT2_ET_DIR_NO_SPACE && ext2fs_expand_dir(e2fs, ino)) {
            free(p_path);
            return -ENOSPC;

		}
	} while (rc == EXT2_ET_DIR_NO_SPACE);

	if (rc) {
        free(p_path);
		return -EIO;
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
		inode.i_uid = ctx->uid;
		inode.i_gid = ctx->gid;
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

	if (S_ISLNK(mode) && fastsymlink != NULL) {
		inode.i_size = strlen(fastsymlink);
		strncpy((char *)&(inode.i_block[0]),fastsymlink,
				(EXT2_N_BLOCKS * sizeof(inode.i_block[0])));
	}

	rc = ext2fs_write_new_inode(e2fs, n_ino, &inode);
	if (rc) {

        free(p_path);
		return -EIO;
	}

	/* update parent dir */
	rt = readNode(e2fs, p_path, &ino, &inode);
	if (rt) {

        free(p_path);
		return -EIO;
	}
	inode.i_ctime = inode.i_mtime = tm;
	rc = writeNode(e2fs, ino, &inode);
	if (rc) {

        free(p_path);
		return -EIO;
	}

    free(p_path);
	return 0;
}

int op_create (const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int rt;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s, mode: 0%o", path, mode);

	if (supFS_open(path, fi) == 0) {
		debugf("leave");
		return 0;
	}

	rt = supFS_create(e2fs, path, mode, 0, NULL);
	if (rt != 0) {
		return rt;
	}

	if (supFS_open(path, fi)) {
		debugf("supFS_open(path, fi); failed");
		return -EIO;
	}

	debugf("leave");
	return 0;
}


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


int supFS_getattr (const char *path, struct stat *stbuf)
{
	int returnValue;
	ext2_ino_t ino;
	struct ext2_inode inode;
	ext2_filsys e2fs = current_ext2fs();

	returnValue = check(path);
	if (returnValue != 0) {
        char logError[100]="check() failed";
        strcat(logError,path);
        log_error(logError);
		return returnValue;
	}

	returnValue = readNode(e2fs, path, &ino, &inode);
	if (returnValue) {
        char loError[1000]="readNode() FAILED :";
        strcat(loError,path);
        log_error(loError);
		return returnValue;
	}
    fillstatbuffer(e2fs, ino, &inode, stbuf);

	return 0;
}


void * op_init (struct fuse_conn_info *conn)
{
	errcode_t rc;
	struct fuse_context *cntx=fuse_get_context();
	struct supFs_data *e2data=cntx->private_data;

	rc = ext2fs_open(e2data->device, EXT2_FLAG_RW, 0, 0, unix_io_manager, &e2data->e2fs);
	if (rc) {
		log_error("Error while trying to open device");
		exit(1);
	}

	rc = ext2fs_read_bitmaps(e2data->e2fs);
	if (rc) {
		log_error("Error while reading bitmaps");
		ext2fs_close(e2data->e2fs);
		exit(1);
	}
	log_info("FileSystem ReadWrite");


	return e2data;
}


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

	rt = checkToDir(path, &p_path, &r_path);
	if (rt != 0) {
		debugf("check(%s); failed", path);
		return rt;
	}

	debugf("parent: %s, child: %s", p_path, r_path);

	rt = readNode(e2fs, p_path, &ino, &inode);
	if (rt) {
		debugf("readNode(%s, &ext2Ino, &ext2Inode); failed", p_path);
		free(p_path);
		return rt;
	}

	do {
		debugf("calling ext2fs_mkdir(e2fs, %d, 0, %s);", ino, r_path);
		rc = ext2fs_mkdir(e2fs, ino, 0, r_path);
		if (rc == EXT2_ET_DIR_NO_SPACE) {
			debugf("calling ext2fs_expand_dir(e2fs, &d)", ino);
			if (ext2fs_expand_dir(e2fs, ino)) {
				debugf("error while expanding directory %s (%d)", p_path, ino);
                free(p_path);
				return -ENOSPC;
			}
		}
	} while (rc == EXT2_ET_DIR_NO_SPACE);
	if (rc) {
		debugf("ext2fs_mkdir(e2fs, %d, 0, %s); failed (%d)", ino, r_path, rc);
		debugf("e2fs: %p, e2fs->inode_map: %p", e2fs, e2fs->inode_map);
        free(p_path);
		return -EIO;
	}

	rt = readNode(e2fs, path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ext2Ino, &ext2Inode); failed", path);
        free(p_path);
		return -EIO;
	}
	tm = e2fs->now ? e2fs->now : time(NULL);
	inode.i_mode = LINUX_S_IFDIR | mode;
	inode.i_ctime = inode.i_atime = inode.i_mtime = tm;
	ctx = fuse_get_context();
	if (ctx) {
		inode.i_uid = ctx->uid;
		inode.i_gid = ctx->gid;
	}
	rc = writeNode(e2fs, ino, &inode);
	if (rc) {

        free(p_path);
		return -EIO;
	}

	/* update parent dir */
	rt = readNode(e2fs, p_path, &ino, &inode);
	if (rt) {
		debugf("do_readinode(%s, &ext2Ino, &ext2Inode); dailed", p_path);
        free(p_path);
		return -EIO;
	}
	inode.i_ctime = inode.i_mtime = tm;
	rc = writeNode(e2fs, ino, &inode);
	if (rc) {

        free(p_path);
		return -EIO;
	}

    free(p_path);

	debugf("leave");
	return 0;
}


int op_mknod (const char *path, mode_t mode, dev_t dev)
{
	int rt;
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s 0%o", path, mode);

	rt = supFS_create(e2fs, path, mode, dev, NULL);

	debugf("leave");
	return rt;
}


ext2_file_t process_open (ext2_filsys e2fs, const char *path, int flags)
{
	int returnValue;
	errcode_t errcode;
	ext2_ino_t ino;
	ext2_file_t file;
	struct ext2_inode ext2Inode;
	struct fuse_context *context = fuse_get_context();
	struct supFs_data *e2data = context->private_data;

	returnValue = check(path);
	if (returnValue != 0) {
        char logError[100]="check() failed";
        strcat(logError,path);
        log_error(logError);
		return NULL;
	}

	returnValue = readNode(e2fs, path, &ino, &ext2Inode);
	if (returnValue) {
        char logError[100]="readNode() failed";
        strcat(logError,path);
        log_error(logError);
		return NULL;
	}

	errcode = ext2fs_file_open2(
			e2fs,
			ino,
			&ext2Inode,
			(((flags & O_ACCMODE) != 0) ? EXT2_FILE_WRITE : 0) | EXT2_FILE_SHARED_INODE, 
			&file);
	if (errcode) {
		return NULL;
	}

	return file;
}

int supFS_open (const char *path, struct fuse_file_info *fi)
{
	ext2_file_t file;
	ext2_filsys e2fs = current_ext2fs();

	file = process_open(e2fs, path, fi->flags);
	if (file == NULL) {
		return -ENOENT;
	}
	fi->fh = (uint64_t) file;

	return 0;
}


int op_read (const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	__u64 pos;
	errcode_t rc;
	unsigned int bytes;
	ext2_file_t efile = EXT2FS_FILE(fi->fh);
	ext2_filsys e2fs = current_ext2fs();

	debugf("enter");
	debugf("path = %s", path);

	efile = process_open(e2fs, path, O_RDONLY);
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
	
	rt = readNode(e2fs, path, &ino, &inode);
	if (rt) {
		debugf("readNode(%s, &ext2Ino, &ext2Inode); failed", path);
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

	rt = checkToDir(source, &p_src, &r_src);
	if (rt != 0) {
		debugf("check(%s); failed", source);
		return rt;
	}

	debugf("src_parent: %s, src_child: %s", p_src, r_src);

	rt = checkToDir(dest, &p_dest, &r_dest);
	if (rt != 0) {
		debugf("check(%s); failed", dest);
		return rt;
	}

	debugf("dest_parent: %s, dest_child: %s", p_dest, r_dest);

	rt = readNode(e2fs, p_src, &d_src_ino, &d_src_inode);
	if (rt != 0) {
		debugf("readNode(%s, &d_src_ino, &d_src_inode); failed", p_src);
		goto out;
	}

	rt = readNode(e2fs, p_dest, &d_dest_ino, &d_dest_inode);
	if (rt != 0) {
		debugf("readNode(%s, &d_dest_ino, &d_dest_inode); failed", p_dest);
		goto out;
	}

	rt = readNode(e2fs, source, &src_ino, &src_inode);
	if (rt != 0) {
		debugf("readNode(%s, &src_ino, &src_inode); failed", p_dest);
		goto out;
	}

	rt = readNode(e2fs, dest, &dest_ino, &dest_inode);
	if (rt != 0 && rt != -ENOENT) {
		debugf("readNode(%s, &dest_ino, &dest_inode); failed", dest);
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
			goto out;
		}
		rt = readNode(e2fs, p_dest, &d_dest_ino, &d_dest_inode);
		if (rt != 0) {
			debugf("readNode(%s, &d_dest_ino, &d_dest_inode); failed", p_dest);
			goto out;
		}
	}
  	
	/* Step 2: add the link */
	do {
		debugf("calling ext2fs_link(e2fs, %d, %s, %d, %d);", d_dest_ino, r_dest, src_ino,
               modeToExt2Flag(src_inode.i_mode));
		rc = ext2fs_link(e2fs, d_dest_ino, r_dest, src_ino, modeToExt2Flag(src_inode.i_mode));
		if (rc == EXT2_ET_DIR_NO_SPACE) {
			debugf("calling ext2fs_expand_dir(e2fs, &d)", src_ino);
			if (ext2fs_expand_dir(e2fs, d_dest_ino)) {
				debugf("error while expanding directory %s (%d)", p_dest, d_dest_ino);
				rt = -ENOSPC;
				goto out;
			}
			/* ext2fs_expand_dir changes d_dest_inode */
			rt = readNode(e2fs, p_dest, &d_dest_ino, &d_dest_inode);
			if (rt != 0) {
				debugf("readNode(%s, &d_dest_ino, &d_dest_inode); failed", p_dest);
				goto out;
			}
		}
	} while (rc == EXT2_ET_DIR_NO_SPACE);
	if (rc != 0) {
		debugf("ext2fs_link(e2fs, %d, %s, %d, %d); failed", d_dest_ino, r_dest, src_ino,
               modeToExt2Flag(src_inode.i_mode));
		rt = -EIO;
		goto out;
	}

	/* Special case: if moving dir across different parents fix counters and '..' */
	if (LINUX_S_ISDIR(src_inode.i_mode) && d_src_ino != d_dest_ino) {
		d_dest_inode.i_links_count++;
		if (d_src_inode.i_links_count > 1) {
			d_src_inode.i_links_count--;
		}
		rc = writeNode(e2fs, d_src_ino, &d_src_inode);
		if (rc != 0) {
			debugf("writeNode(e2fs, src_ino, &src_inode); failed");
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
	rt = writeNode(e2fs, d_dest_ino, &d_dest_inode);
	if (rt != 0) {
		debugf("writeNode(e2fs, d_dest_ino, &d_dest_inode); failed");
		goto out;
	}
	rt = writeNode(e2fs, src_ino, &src_inode);
	if (rt != 0) {
		debugf("writeNode(e2fs, src_ino, &src_inode); failed");
		goto out;
	}
	debugf("done");

	/* Step 3: delete the source */

	rc = ext2fs_unlink(e2fs, d_src_ino, r_src, src_ino, 0);
	if (rc) {
		debugf("while unlinking src ext2Ino %d", (int) src_ino);
		rt = -EIO;
		goto out;
	}

out:
    free(p_src);
    free(p_dest);
	return rt;
}

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

	rt = checkToDir(path, &p_path, &r_path);
	if (rt != 0) {
		debugf("checkToDir: failed");
		return rt;
	}

	debugf("parent: %s, child: %s", p_path, r_path);
	
	rt = readNode(e2fs, p_path, &p_ino, &p_inode);
	if (rt) {
		debugf("do_readinode(%s, &p_ino, &p_inode); failed", p_path);
        free(p_path);
		return rt;
	}
	rt = readNode(e2fs, path, &r_ino, &r_inode);
	if (rt) {
		debugf("do_readinode(%s, &r_ino, &r_inode); failed", path);
        free(p_path);
		return rt;
		
	}
	if (!LINUX_S_ISDIR(r_inode.i_mode)) {
		debugf("%s is not a directory", path);
        free(p_path);
		return -ENOTDIR;
	}
	if (r_ino == EXT2_ROOT_INO) {
		debugf("root dir cannot be removed", path);
        free(p_path);
		return -EIO;
	}
	
	rt = do_check_empty_dir(e2fs, r_ino);
	if (rt) {
		debugf("do_check_empty_dir filed");
        free(p_path);
		return rt;
	}

	rc = ext2fs_unlink(e2fs, p_ino, r_path, r_ino, 0);
	if (rc) {
		debugf("while unlinking ext2Ino %d", (int) r_ino);
        free(p_path);
		return -EIO;
	}

	rt = changeFileInode(e2fs, r_ino, &r_inode);
	if (rt) {
		debugf("changeFileInode(r_ino, &r_inode); failed");
        free(p_path);
		return rt;
	}

	rt = readNode(e2fs, p_path, &p_ino, &p_inode);
	if (rt) {
		debugf("do_readinode(p_path, &p_ino, &p_inode); failed");
        free(p_path);
		return rt;
	}
	if (p_inode.i_links_count > 1) {
		p_inode.i_links_count--;
	}
	p_inode.i_mtime = e2fs->now ? e2fs->now : time(NULL);
	p_inode.i_ctime = e2fs->now ? e2fs->now : time(NULL);
	rc = writeNode(e2fs, p_ino, &p_inode);
	if (rc) {

        free(p_path);
		return -EIO;
	}

    free(p_path);

	debugf("leave");
	return 0;
}

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

	rt = check(path);
	if (rt != 0) {
		debugf("check(%s); failed", path);
		return rt;
	}
	efile = process_open(e2fs, path, O_WRONLY);
	if (efile == NULL) {
		debugf("process_open(%s); failed", path);
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

	rt = readNode(e2fs, path, &ino, &inode);
	if (rt) {
		debugf("readNode(%s, &ext2Ino, &vnode); failed", path);
		do_release(efile);
		return rt;
	}
	inode.i_ctime = e2fs->now ? e2fs->now : time(NULL);
	rt = writeNode(e2fs, ino, &inode);
	if (rt) {
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

	rt = check(path);
	if (rt != 0) {
		debugf("check(%s); failed", path);
		return rt;
	}

	rt = checkToDir(path, &p_path, &r_path);
	if (rt != 0) {
		debugf("checkToDir: failed");
		return rt;
	}

	debugf("parent: %s, child: %s", p_path, r_path);

	rt = readNode(e2fs, p_path, &p_ino, &p_inode);
	if (rt) {
		debugf("do_readinode(%s, &p_ino, &p_inode); failed", path);
        free(p_path);
		return rt;
	}
	rt = readNode(e2fs, path, &r_ino, &r_inode);
	if (rt) {
		debugf("do_readinode(%s, &r_ino, &r_inode); failed", path);
        free(p_path);
		return rt;
	}

	if (LINUX_S_ISDIR(r_inode.i_mode)) {
		debugf("%s is a directory", path);
        free(p_path);
		return -EISDIR;
	}

	rc = ext2fs_unlink(e2fs, p_ino, r_path, r_ino, 0);
	if (rc) {
		debugf("ext2fs_unlink(e2fs, %d, %s, %d, 0); failed", p_ino, r_path, r_ino);
        free(p_path);
		return -EIO;
	}

	p_inode.i_ctime = p_inode.i_mtime = e2fs->now ? e2fs->now : time(NULL);
	rt = writeNode(e2fs, p_ino, &p_inode);
	if (rt) {
		debugf("writeNode(e2fs, p_ino, &p_inode); failed");
        free(p_path);
		return -EIO;
	}

	if (r_inode.i_links_count > 0) {
		r_inode.i_links_count -= 1;
	}
	r_inode.i_ctime = e2fs->now ? e2fs->now : time(NULL);
	rc = writeNode(e2fs, r_ino, &r_inode);
	if (rc) {
		debugf("writeNode(e2fs, &r_ino, &r_inode); failed");
        free(p_path);
		return -EIO;
	}

    free(p_path);
	debugf("leave");
	return 0;
}


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
		debugf("returnValue: %d, size: %u, written: %u", rt, size, wr);
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

	efile = process_open(e2fs, path, O_WRONLY);
	rt = do_write(efile, buf, size, offset);
	do_release(efile);

	debugf("leave");
	return rt;
}
