#include "main.h"

ext2_filsys getCurrent_e2fs(void) {

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

static int blocksRelease(ext2_filsys fs, blk_t *blocknr, int blockcnt, void *private)
{
	blk_t block;
	block = *blocknr;
	ext2fs_block_alloc_stats(fs, block, -1);
	return 0;
}

int changeFileInode(ext2_filsys e2fs, ext2_ino_t ext2Ino, struct ext2_inode *inode)
{
	errcode_t errorCode;
	char blockBuffer[3*e2fs->blocksize];

	inode->i_links_count = 0;
	inode->i_dtime = time(NULL);

	errorCode = ext2fs_write_inode(e2fs, ext2Ino, inode);
	if (errorCode) {
        return -EIO;
	}

	if (ext2fs_inode_has_valid_blocks(inode)) {
        ext2fs_block_iterate(e2fs, ext2Ino, 0, blockBuffer, blocksRelease, NULL);
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
	int returnValue;
	ext2_filsys e2fs = getCurrent_e2fs();
	
	returnValue = check(path);
	if (returnValue != 0) {
		return returnValue;
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

int createNode(ext2_filsys e2fs, const char *path, mode_t mode, dev_t dev, const char *fastsymlink)
{
	int returnValue;

	char *actualPath;
	char *objectivePpath;

	ext2_ino_t dirNode;
	struct ext2_inode inode;
	ext2_ino_t newNode;

	struct fuse_context *context;

    // from path get actuelPath add objectivePath
	checkToDir(path, &actualPath, &objectivePpath);
    // from actualPath get node
	returnValue = readNode(e2fs, actualPath, &dirNode, &inode);
    // check status
	if (returnValue) {
        free(actualPath);
		return returnValue;
	}

    // check status
	if (ext2fs_new_inode(e2fs, dirNode, mode, 0, &newNode)) {
        free(actualPath);
		return -ENOMEM;
	}

    // apply link to new file
    errcode_t errorCode;
	do {
        // check fail + apply exampend space disk
        errorCode = ext2fs_link(e2fs, dirNode, objectivePpath, newNode, modeToExt2Flag(mode));
		if (errorCode == EXT2_ET_DIR_NO_SPACE) {
            if (ext2fs_expand_dir(e2fs, dirNode)) {
                free(actualPath);
                return -ENOSPC;
            }
		}
	} while (errorCode == EXT2_ET_DIR_NO_SPACE);
    // check status
	if (errorCode) {
        free(actualPath);
		return -EIO;
	}

    // alloc memory to node
	ext2fs_inode_alloc_stats2(e2fs, newNode, +1, 0);
	memset(&inode, 0, sizeof(inode));
    //apply properties
    if (e2fs->now){
        inode.i_atime = inode.i_ctime = inode.i_mtime = e2fs->now;
    } else {
        inode.i_atime = inode.i_ctime = inode.i_mtime = time(NULL);
    }
    inode.i_mode = mode;
	inode.i_links_count = 1;
	inode.i_size = 0;
	context = fuse_get_context();
    // apply perm user/group
	if (context) {
		inode.i_uid = context->uid;
		inode.i_gid = context->gid;
	}

    // write inode
	if (ext2fs_write_new_inode(e2fs, newNode, &inode)) {
        free(actualPath);
		return -EIO;
	}

	// update parent directory
	returnValue = readNode(e2fs, actualPath, &dirNode, &inode);
    free(actualPath);

	if (returnValue) {
		return -EIO;
	}

    // write final inode
	if (writeNode(e2fs, dirNode, &inode)) {
		return -EIO;
	}

	return 0;
}

int supFS_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{

	ext2_filsys ext2Filsys = getCurrent_e2fs();


	if (supFS_open(path, fi) == 0) {
		return 0;
	}

	if (createNode(ext2Filsys, path, mode, 0, NULL) != 0) {
		return createNode(ext2Filsys, path, mode, 0, NULL);
	}

	if (supFS_open(path, fi)) {
        log_error("supFS_open() FAILED");
		return -EIO;
	}

	return 0;
}


void destroy(void *userdata)
{
	ext2_filsys ext2fs = getCurrent_e2fs();

    if (ext2fs_close(ext2fs)) {
        log_error("Error trying to close filesystem ext2");
	}

}


int supFS_flush(const char *path, struct fuse_file_info *fi)
{
	ext2_file_t file = EXT2FS_FILE(fi->fh);

	if(file != NULL){
        if (ext2fs_file_flush(file)) {
            return -EIO;
        }

        return 0;
    } else{
        return -ENOENT;
    }

}


int supFS_getattr (const char *path, struct stat *stbuf)
{
	int returnValue;
	ext2_ino_t ino;
	struct ext2_inode inode;
	ext2_filsys e2fs = getCurrent_e2fs();

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


void * initE2fs(struct fuse_conn_info *conn)
{
	errcode_t errorCode;
	struct fuse_context *cntx=fuse_get_context();
	struct supFs_data *e2data=cntx->private_data;

	errorCode = ext2fs_open(e2data->device, EXT2_FLAG_RW, 0, 0, unix_io_manager, &e2data->e2fs);
	if (errorCode) {
		log_error("Error while trying to open device");
		exit(1);
	}

	errorCode = ext2fs_read_bitmaps(e2data->e2fs);
	if (errorCode) {
		log_error("Error while reading bitmaps");
		ext2fs_close(e2data->e2fs);
		exit(1);
	}
	log_info("FileSystem ReadWrite");

	return e2data;
}


int supFS_mkdir(const char *path, mode_t mode)
{
	int returnValue;

	errcode_t errorCode;

	char *actualPath;
	char *objectivePpath;

	ext2_ino_t dirNode;
	struct ext2_inode inode;

	struct fuse_context *context;

	ext2_filsys e2fs = getCurrent_e2fs();

    // from path to actual and objectivePath
	returnValue = checkToDir(path, &actualPath, &objectivePpath);
	if (returnValue != 0) {
		return returnValue;
	}

    // get dirNode
	returnValue = readNode(e2fs, actualPath, &dirNode, &inode);
	if (returnValue) {
		free(actualPath);
		return returnValue;
	}

	do {
		errorCode = ext2fs_mkdir(e2fs, dirNode, 0, objectivePpath);
		if (errorCode == EXT2_ET_DIR_NO_SPACE) {
			if (ext2fs_expand_dir(e2fs, dirNode)) {
                free(actualPath);
				return -ENOSPC;
			}
		}
        // try while no space
	} while (errorCode == EXT2_ET_DIR_NO_SPACE);

	if (errorCode) {
        free(actualPath);
		return -EIO;
	}

	returnValue = readNode(e2fs, path, &dirNode, &inode);
	if (returnValue) {
        free(actualPath);
		return -EIO;
	}

    //apply properties
    if (e2fs->now){
        inode.i_ctime = inode.i_atime = inode.i_mtime = e2fs->now;
    } else {
        inode.i_ctime = inode.i_atime = inode.i_mtime = time(NULL);
    }

	inode.i_mode = LINUX_S_IFDIR | mode;

	context = fuse_get_context();
    // apply perm user/group
	if (context) {
		inode.i_uid = context->uid;
		inode.i_gid = context->gid;
	}

    // write inode
	errorCode = writeNode(e2fs, dirNode, &inode);
	if (errorCode) {
        free(actualPath);
		return -EIO;
	}

    // update parent directory
	returnValue = readNode(e2fs, actualPath, &dirNode, &inode);
    free(actualPath);
	if (returnValue) {
		return -EIO;
	}

    // write final inode
	errorCode = writeNode(e2fs, dirNode, &inode);
	if (errorCode) {
		return -EIO;
	}

	return 0;
}


int supFS_mknod(const char *path, mode_t mode, dev_t dev)
{
	ext2_filsys e2fs = getCurrent_e2fs();
	return createNode(e2fs, path, mode, dev, NULL);;
}


ext2_file_t process_open (ext2_filsys e2fs, const char *path, int flags)
{
	int returnValue;

	ext2_ino_t ino;
	ext2_file_t file;
	struct ext2_inode ext2Inode;

	returnValue = check(path);
	if (returnValue != 0) {
        log_error("check() failed");
		return NULL;
	}

	returnValue = readNode(e2fs, path, &ino, &ext2Inode);
	if (returnValue) {
        log_error("readNode() failed");
		return NULL;
	}

	if (ext2fs_file_open2(e2fs,ino, &ext2Inode, (((flags & O_ACCMODE) != 0) ? EXT2_FILE_WRITE : 0) | EXT2_FILE_SHARED_INODE, &file)) {
		return NULL;
	}

	return file;
}

int supFS_open (const char *path, struct fuse_file_info *fi)
{
	ext2_file_t file;
	ext2_filsys e2fs = getCurrent_e2fs();

	file = process_open(e2fs, path, fi->flags);
	if (file == NULL) {
		return -ENOENT;
	}
	fi->fh = (uint64_t) file;

	return 0;
}


int supFS_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	__u64 pos;
	errcode_t errorReturnChecker;
	unsigned int readBytesFile;
	ext2_file_t efile = EXT2FS_FILE(fi->fh);
	ext2_filsys e2fs = getCurrent_e2fs();

	// open file
	efile = process_open(e2fs, path, O_RDONLY);
	errorReturnChecker = ext2fs_file_llseek(efile, offset, SEEK_SET, &pos);
	if (errorReturnChecker) {
        releaseFile(efile);
		return -EINVAL;
	}

	// read file content
	errorReturnChecker = ext2fs_file_read(efile, buf, size, &readBytesFile);
	if (errorReturnChecker) {
        releaseFile(efile);
		return -EIO;
	}
    releaseFile(efile);

	return readBytesFile;
}


struct dir_walk_data {
	char *buf;
	fuse_fill_dir_t filler;
};


int parseDirectory(ext2_ino_t dir, int entry, struct ext2_dir_entry *dirent, int offset, int blocksize, char *buf,
				   void *vpsid)
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
	dirent->name[len] = 0;

	switch (dirent->name_len >> 8) {
		case EXT2_FT_UNKNOWN:
            type = DT_UNKNOWN;
            break;
		case EXT2_FT_REG_FILE:
            type = DT_REG;
            break;
		case EXT2_FT_DIR:
            type = DT_DIR;
            break;
		case EXT2_FT_CHRDEV:
            type = DT_CHR;
            break;
		case EXT2_FT_BLKDEV:
            type = DT_BLK;
            break;
		case EXT2_FT_FIFO:
            type = DT_FIFO;
            break;
		case EXT2_FT_SOCK:
            type = DT_SOCK;
            break;
		case EXT2_FT_SYMLINK:
            type = DT_LNK;
            break;
		default:
            type = DT_UNKNOWN;
            break;
	}
	st.st_ino = dirent->inode;
	st.st_mode = type << 12;
	res = psid->filler(psid->buf, dirent->name, &st, 0);
	if (res != 0) {
		return BLOCK_ABORT;
	}
	return 0;
}


int supFS_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{

	ext2_ino_t ext2Ino;
	struct ext2_inode ext2Inode;
	struct dir_walk_data dwd={.buf = buf, .filler = filler};
	ext2_filsys ext2fs = getCurrent_e2fs();
	

	if (readNode(ext2fs, path, &ext2Ino, &ext2Inode)) {
		log_error("readNode() FAILED");
        return readNode(ext2fs, path, &ext2Ino, &ext2Inode);
	}

    if (ext2fs_dir_iterate2(ext2fs, ext2Ino, DIRENT_FLAG_INCLUDE_EMPTY, NULL, parseDirectory, &dwd)) {
		return -EIO;
	}

	return 0;
}


int releaseFile(ext2_file_t file)
{
	if (ext2fs_file_close(file)) {
		return -EIO;
	}
	return 0;
}

int supFS_release(const char *path, struct fuse_file_info *fi)
{
	ext2_file_t file = (ext2_file_t) (unsigned long) fi->fh;


	if (releaseFile(file) != 0) {
		log_error("releaseFile() FAILED");
		return releaseFile(file);
	}

	return 0;
}


static int processToFixHeritage(ext2_ino_t dir EXT2FS_ATTR((unused)), int entry EXT2FS_ATTR((unused)), struct ext2_dir_entry *dirent, int offset EXT2FS_ATTR((unused)), int blocksize EXT2FS_ATTR((unused)), char *buffer EXT2FS_ATTR((unused)), void *private)
{
	ext2_ino_t *ext2Ino = (ext2_ino_t *) private;

	if ((dirent->name_len & 0xFF) == 2 && strncmp(dirent->name, "..", 2) == 0) {
		dirent->inode = *ext2Ino;
		return DIRENT_ABORT | DIRENT_CHANGED;
	} else {
		return 0;
	}
}

static int fixHeritage(ext2_filsys ext2fs, ext2_ino_t ext2Ino, ext2_ino_t dotdot)
{
	if (ext2fs_dir_iterate2(ext2fs, ext2Ino, DIRENT_FLAG_INCLUDE_EMPTY, 0, processToFixHeritage, &dotdot)) {
		return -EIO;
	}
	return 0;
}

int supFS_rename(const char *actual_path, const char *objectif_path)
{
	int returnValue;
	errcode_t errcode;

	char *parent_source;
	char *child_source;
	char *parent_destinataire;
	char *child_destinataire;

	ext2_ino_t parent_source_ino;
	ext2_ino_t parent_destinataire_ino;
	ext2_ino_t child_source_ino;
	ext2_ino_t child_destinataire_ino;

	struct ext2_inode parent_source_inode;
	struct ext2_inode parent_destinataire_inode;
	struct ext2_inode child_source_inode;
	struct ext2_inode child_destinataire_inode;

	ext2_filsys ext2fs = getCurrent_e2fs();


	returnValue = checkToDir(actual_path, &parent_source, &child_source);
	if (returnValue != 0) {
		log_error("check() FAILED");
		return returnValue;
	}

	returnValue = checkToDir(objectif_path, &parent_destinataire, &child_destinataire);
	if (returnValue != 0) {
        log_error("check() FAILED");
		return returnValue;
	}

	returnValue = readNode(ext2fs, parent_source, &child_source_ino, &child_source_inode);
	if (returnValue != 0) {
        log_error("readNode() FAILED");
        return returnValue;
	}

	returnValue = readNode(ext2fs, parent_destinataire, &child_destinataire_ino, &child_destinataire_inode);
	if (returnValue != 0) {
        log_error("readNode() FAILED");
        return returnValue;
	}

	returnValue = readNode(ext2fs, actual_path, &parent_source_ino, &parent_source_inode);
	if (returnValue != 0) {
        log_error("readNode() FAILED");
        return returnValue;
	}

	returnValue = readNode(ext2fs, objectif_path, &parent_destinataire_ino, &parent_destinataire_inode);
	if (returnValue != 0 && returnValue != -ENOENT) {
        log_error("readNode() FAILED");
        return returnValue;
	}

    // Check oldpath and newpath if are not null , return a succes statut
	if (returnValue == 0 && parent_source_ino == parent_destinataire_ino) {
        return returnValue;
	}

    // the objectif_path have a old's prefix path , for let fuse check this

	if (returnValue == 0) {
		if (LINUX_S_ISDIR(parent_destinataire_inode.i_mode)) {

			if (!(LINUX_S_ISDIR(parent_source_inode.i_mode))) {
				return -EISDIR;

			}
			// check if the objectif_path if not empty
			returnValue = checkDirIsEmpty(ext2fs, parent_destinataire_ino);
			if (returnValue != 0) {
                return returnValue;
			}
		}
		// actual_path is a directory and the objectif_path are not
		if (LINUX_S_ISDIR(parent_source_inode.i_mode) &&
		    !(LINUX_S_ISDIR(parent_destinataire_inode.i_mode))) {
			return -ENOTDIR;
		}

		// At first if actual-destinataire exist delete him
		if (LINUX_S_ISDIR(parent_destinataire_inode.i_mode)) {
			errcode = op_rmdir(objectif_path);
		} else {
			errcode = supFS_unlink(objectif_path);
		}
		if (errcode) {
			return returnValue;
		}
		returnValue = readNode(ext2fs, parent_destinataire, &child_destinataire_ino, &child_destinataire_inode);
		if (returnValue != 0) {
			log_error("readNode() FAILED");
            return returnValue;
		}
	}
  	
	// Second time create a new link
	do {
		errcode = ext2fs_link(ext2fs, child_destinataire_ino, child_destinataire, parent_source_ino, modeToExt2Flag(parent_source_inode.i_mode));
		if (errcode == EXT2_ET_DIR_NO_SPACE) {
			if (ext2fs_expand_dir(ext2fs, child_destinataire_ino)) {
                return -ENOSPC;
			}
			returnValue = readNode(ext2fs, parent_destinataire, &child_destinataire_ino, &child_destinataire_inode);
			if (returnValue != 0) {
                log_error("readNode() FAILED");
                return returnValue;
			}
		}
	} while (errcode == EXT2_ET_DIR_NO_SPACE);
	if (errcode != 0) {
		return -EIO;
	}

	// Optional if you move a dir get a parametre their parents and son ../ /*
	if (LINUX_S_ISDIR(parent_source_inode.i_mode) && child_source_ino != child_destinataire_ino) {
		child_destinataire_inode.i_links_count++;
		if (child_source_inode.i_links_count > 1) {
			child_source_inode.i_links_count--;
		}
		errcode = writeNode(ext2fs, child_source_ino, &child_source_inode);
		if (errcode != 0) {
			log_error("writeNode() FAILED");
			return -EIO;
		}
		returnValue = fixHeritage(ext2fs, parent_source_ino, child_destinataire_ino);
		if (returnValue != 0) {
            log_error("fixHeritage() FAILED");
            return returnValue;
		}
	}

	//Update utime and unode
	child_destinataire_inode.i_mtime = child_destinataire_inode.i_ctime = parent_source_inode.i_ctime = ext2fs->now ? ext2fs->now : time(NULL);
	returnValue = writeNode(ext2fs, child_destinataire_ino, &child_destinataire_inode);
	if (returnValue != 0) {
        log_error("writeNode() FAILED");
        return returnValue;
	}
	returnValue = writeNode(ext2fs, parent_source_ino, &parent_source_inode);
	if (returnValue != 0) {
        log_error("writeNode() FAILED");
        return returnValue;
	}

	/* Step 3: delete the source */

	errcode = ext2fs_unlink(ext2fs, child_source_ino, child_source, parent_source_ino, 0);
	if (errcode) {
		return -EIO;
	}
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

int checkDirIsEmpty(ext2_filsys ext2_fs, ext2_ino_t ext2Ino)
{
	int empty = 1;

	if (ext2fs_dir_iterate2(ext2_fs, ext2Ino, 0, 0, rmdir_proc, &empty)) {
		return -EIO;
	}

	if (empty == 0) {
		return -ENOTEMPTY;
	}

	return 0;
}

int op_rmdir (const char *path)
{
	int returnValue;
	errcode_t rc;

	char *p_path;
	char *r_path;

	ext2_ino_t p_ino;
	struct ext2_inode p_inode;
	ext2_ino_t r_ino;
	struct ext2_inode r_inode;
	
	ext2_filsys e2fs = getCurrent_e2fs();

	debugf("enter");
	debugf("path = %s", path);

	returnValue = checkToDir(path, &p_path, &r_path);
	if (returnValue != 0) {
		debugf("checkToDir: failed");
		return returnValue;
	}

	debugf("parent: %s, child: %s", p_path, r_path);
	
	returnValue = readNode(e2fs, p_path, &p_ino, &p_inode);
	if (returnValue) {
		debugf("do_readinode(%s, &p_ino, &p_inode); failed", p_path);
        free(p_path);
		return returnValue;
	}
	returnValue = readNode(e2fs, path, &r_ino, &r_inode);
	if (returnValue) {
		debugf("do_readinode(%s, &r_ino, &r_inode); failed", path);
        free(p_path);
		return returnValue;
		
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
	
	returnValue = checkDirIsEmpty(e2fs, r_ino);
	if (returnValue) {
		debugf("checkDirIsEmpty filed");
        free(p_path);
		return returnValue;
	}

	rc = ext2fs_unlink(e2fs, p_ino, r_path, r_ino, 0);
	if (rc) {
		debugf("while unlinking ext2Ino %d", (int) r_ino);
        free(p_path);
		return -EIO;
	}

	returnValue = changeFileInode(e2fs, r_ino, &r_inode);
	if (returnValue) {
		debugf("changeFileInode(r_ino, &r_inode); failed");
        free(p_path);
		return returnValue;
	}

	returnValue = readNode(e2fs, p_path, &p_ino, &p_inode);
	if (returnValue) {
		debugf("do_readinode(p_path, &p_ino, &p_inode); failed");
        free(p_path);
		return returnValue;
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

int supFS_unlink(const char *path)
{
	int returnValue;
	errcode_t errcode;

	char *parentDir;
	char *fileName;

	ext2_ino_t ext2Ino;
	ext2_ino_t rIno;
	struct ext2_inode parentInode;
	struct ext2_inode destInode;

	ext2_filsys e2fs = getCurrent_e2fs();

	returnValue = check(path);
	if (returnValue != 0) {
		log_error("check unlink failed");
		return returnValue;
	}

	returnValue = checkToDir(path, &parentDir, &fileName);
	if (returnValue != 0) {
		log_error("checkToDir failed");
		return returnValue;
	}

    returnValue = readNode(e2fs, path, &rIno, &destInode);
    if (returnValue) {
        log_error("readinode 2 failed");
        free(parentDir);
        return returnValue;
    }
	returnValue = readNode(e2fs, parentDir, &ext2Ino, &parentInode);
	if (returnValue) {
		log_error("readinode failed");
        free(parentDir);
		return returnValue;
	}

	if (LINUX_S_ISDIR(destInode.i_mode)) {
		log_error("It is a directory");
        free(parentDir);
		return -EISDIR;
	}

	errcode = ext2fs_unlink(e2fs, ext2Ino, fileName, rIno, 0);
	if (errcode) {
		log_error("ext2fs_unlink failed");
        free(parentDir);
		return -EIO;
	}

	parentInode.i_ctime = parentInode.i_mtime = e2fs->now;
	returnValue = writeNode(e2fs, ext2Ino, &parentInode);
	if (returnValue) {
		log_error("writeNode failed");
        free(parentDir);
		return -EIO;
	}

	if (destInode.i_links_count > 0) {
		destInode.i_links_count -= 1;
	}
	destInode.i_ctime = e2fs->now;
	errcode = writeNode(e2fs, rIno, &destInode);
	if (errcode) {
		log_error("writeNode2 failed");
        free(parentDir);
		return -EIO;
	}

    free(parentDir);
	return 0;
}


size_t process_write(ext2_file_t file, const char *buf, size_t size, off_t offset)
{
	int returnValue;
	const char *src;
	unsigned int writeValue;
	unsigned long long npos;
	unsigned long long filesize;

	returnValue = ext2fs_file_get_lsize(file,  &filesize);
	if (returnValue != 0) {
		return returnValue;
	}
	if (offset + size > filesize) {
		returnValue = ext2fs_file_set_size2(file, offset + size);
		if (returnValue) {
			return returnValue;
		}
	}

	returnValue = ext2fs_file_llseek(file, offset, SEEK_SET, &npos);
	if (returnValue) {
		return returnValue;
	}

	for (returnValue = 0, writeValue = 0, src = buf; size > 0 && returnValue == 0; size -= writeValue, src += writeValue) {
		returnValue = ext2fs_file_write(file, src, size, &writeValue);
	}
	if (returnValue) {
		return returnValue;
	}

	returnValue = ext2fs_file_flush(file);
	if (returnValue) {
		return returnValue;
	}

	return writeValue;
}

int supFS_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    size_t returnValue;

    ext2_filsys ext2fs = getCurrent_e2fs();
    ext2_file_t file = EXT2FS_FILE(fi->fh);

	file = process_open(ext2fs , path, O_WRONLY);
    returnValue = process_write(file, buf, size, offset);
    releaseFile(file);


	return (int)returnValue;
}
