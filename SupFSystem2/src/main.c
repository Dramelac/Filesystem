#include "main.h"


int process_error(int value){
    if(value<0){
        return -errno;
    }
    return value;
}

static void supFS_fullpath(char fullPath[PATH_MAX],const char *path){

    strcpy(fullPath, SUPFS_DATA->rootDir);//pwd = rootdir
    strncat(fullPath,path,PATH_MAX);//rootdir + shortpath

}

static int supFS_getattr(const char *path, struct stat *statbuffer) {
    int rt;
    ext2_ino_t ino;
    struct ext2_inode inode;
    ext2_filsys e2fs = current_ext2fs();
    //log_info("getAttr path :", path);
    log_error("C");
    rt = do_check(path);
    log_error("D");
    if (rt != 0) {
        log_error("do check error");
        return rt;
    }
    rt = do_readinode(e2fs, path, &ino, &inode);
    log_error("E");
    if (rt) {
        //log_info("do_readinode(%s, &ino, &vnode); failed / ", path);
        return rt;
    }
    log_error("F");
    do_fillstatbuf(e2fs, ino, &inode, statbuffer);
    log_info("succes", "getAttr");
    return 0;
}

static int supFS_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi) {

    int rt;
    errcode_t rc;
    ext2_ino_t ino;
    struct ext2_inode inode;
    struct dir_walk_data dwd={
            .buf = buf,
            .filler = filler};
    ext2_filsys e2fs = current_ext2fs();

    rt = do_readinode(e2fs, path, &ino, &inode);
    if (rt) {
        log_info("do_readinode(%s, &ino, &inode); failed", path);
        return rt;
    }

    rc = ext2fs_dir_iterate2(e2fs,ino, DIRENT_FLAG_INCLUDE_EMPTY, NULL, walk_dir, &dwd);

    if (rc) {
        log_info("Error while trying to ext2fs_dir_iterate %s", path);
        return -EIO;
    }

    return 0;
}

static int supFS_open(const char *path, struct fuse_file_info *fileInfo) {

    ext2_file_t efile;
    ext2_filsys e2fs = current_ext2fs();

    efile = do_open(e2fs, path, fileInfo->flags);
    if (efile == NULL) {
        log_error("do_open; failed");
        return -ENOENT;
    }
    fileInfo->fh = (uint64_t) efile;

    return 0;
}

static int supFS_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {

    __u64 pos;
    errcode_t rc;
    unsigned int bytes;
    ext2_file_t efile = EXT2FS_FILE(fi->fh);
    ext2_filsys e2fs = current_ext2fs();

    efile = do_open(e2fs, path, O_RDONLY);
    rc = ext2fs_file_llseek(efile, offset, SEEK_SET, &pos);
    if (rc) {
        fs_release(efile);
        return -EINVAL;
    }

    rc = ext2fs_file_read(efile, buf, size, &bytes);
    if (rc) {
        fs_release(efile);
        return -EIO;
    }
    return bytes;
}

int supFS_access(const char *path, int mask){

    char fullPath[PATH_MAX];

    supFS_fullpath(fullPath,path);

    return process_error(access(fullPath,mask));

}

int supFS_opendir(const char *path, struct fuse_file_info *fi)
{
    DIR *direntFileHandle;
    int returnV = 0;
    char fullPath[PATH_MAX];

    supFS_fullpath(fullPath, path);

    // open dir + check content
    direntFileHandle = opendir(fullPath);
    if (direntFileHandle == NULL) {
        returnV = log_error("supFS_opendir");
    }

    fi->fh = (intptr_t) direntFileHandle;

    return returnV;
}

int supFS_truncate(const char *path, off_t newsize)
{
    char fullPath[PATH_MAX];

    supFS_fullpath(fullPath, path);
    return process_error(truncate(fullPath, newsize));

}

static int supFS_write(const char *path, const char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {


    return process_error(pwrite(fi->fh, buf, size, offset));

}

int supFS_rename(const char *path, const char *newpath)
{
    char fullPath[PATH_MAX];
    char fullnewpath[PATH_MAX];

    supFS_fullpath(fullPath, path);
    supFS_fullpath(fullnewpath, newpath);

    return process_error(rename(fullPath, fullnewpath));



}

int supFS_mknod(const char *path, mode_t mode, dev_t dev)
{
    int returnV;
    char fullPath[PATH_MAX];

    supFS_fullpath(fullPath, path);

    // check different file type
    if (S_ISREG(mode)) {
        returnV = open(fullPath, O_CREAT | O_EXCL | O_WRONLY, mode);
        if (returnV >= 0){
            returnV = close(returnV);
        }
    } else
    if (S_ISFIFO(mode)) {
        returnV = mkfifo(fullPath, mode);
    }
    else {
        returnV = mknod(fullPath, mode, dev);
    }

    return returnV;
}

int supFS_chmod(const char *path, mode_t mode)
{
    char fpath[PATH_MAX];

    supFS_fullpath(fpath, path);

    return process_error(chmod(fpath, mode));
}

int supFS_chown(const char *path, uid_t uid, gid_t gid)

{
    char fpath[PATH_MAX];

    supFS_fullpath(fpath, path);

    return process_error(chown(fpath, uid, gid));
}

int supFS_mkdir(const char *path, mode_t mode){

    char fullPath[PATH_MAX];
    supFS_fullpath(fullPath,path);

    return process_error(mkdir(fullPath,mode));

}

int supFS_rmdir(const char *path){
    char fullPath[PATH_MAX];
    supFS_fullpath(fullPath,path);
    return process_error(rmdir(fullPath));
}

//For remove a file in directory
int supFS_unlink(const char *path){
    char fullPath[PATH_MAX];
    supFS_fullpath(fullPath,path);

    return process_error(unlink(fullPath));

}

int supFS_utime(const char *path, struct utimbuf *ubuf)
{
    char fullPath[PATH_MAX];
    supFS_fullpath(fullPath, path);

    return process_error(utime(fullPath, ubuf));
}

static struct fuse_operations fuseStruct_callback = {
        .getattr = supFS_getattr,
        .open = supFS_open,
        .read = supFS_read,
        .readdir = supFS_readdir,
        .opendir = supFS_open,
        //.access = supFS_access,
        //.write = supFS_write,
        //.rename = supFS_rename,
        //.mknod = supFS_mknod,
        //.mkdir = supFS_mkdir,
        //.rmdir = supFS_rmdir,
        //.unlink = supFS_unlink,
        //.truncate = supFS_truncate,
        //.chmod = supFS_chmod,
        //.chown = supFS_chown,
        //.utime = supFS_utime,
};

int main(int argc, char *argv[])
{
    /*
    if ((getuid() == 0) || (geteuid() == 0)) {
        printf("Root privilege forbidden, security fault.\n");
        return 1;
    }*/


    int fuse_return;
    struct supFS_state *supfs_data;

    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-')) {
        // check arg count + last two arg not options but real directory
        printf("USAGE : command [options] mountDirectory rootDirectory");
    }

    supfs_data = malloc(sizeof(struct supFS_state));
    if (supfs_data == NULL) {
        printf("error mallox data");
        return -1;
    }


    supfs_data->rootDir = realpath(argv[argc-1], NULL);
    // remove rootDir
    argv[argc-1] = NULL;
    argc--;

    log_info("Starting fuse FS", "");
    // run fuce
    fuse_return = fuse_main(argc, argv, &fuseStruct_callback, supfs_data);

    // print return value
    printf("fuse return %d\n", fuse_return);
    log_info("fuse terminated", "");

    free(supfs_data);
    return fuse_return;
}
