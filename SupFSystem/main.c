#define FUSE_USE_VERSION 26

#include "SupFS_log.h"
#include <fuse.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

struct supFS_state{
    char *rootDir;
};
#define SUPFS_DATA ((struct supFS_state *) fuse_get_context()->private_data)

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
    int returnValue;
    char fullPath[PATH_MAX];

    supFS_fullpath(fullPath, path);

    returnValue = lstat(fullPath, statbuffer);
    if (returnValue < 0){
        returnValue = log_error("getAttr");
    }

    return returnValue;
}

static int supFS_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi) {

    int returnValue = 0;
    DIR *direntFileHandle;
    struct dirent *structdirent;


    direntFileHandle = (DIR *) (uintptr_t) fi->fh;

    structdirent = readdir(direntFileHandle);

    if (structdirent == 0) {
        returnValue = log_error("supFS_readdir");
        return returnValue;
    }

    do {
        if (filler(buf, structdirent->d_name, NULL, 0) != 0) {
            return -ENOMEM;
        }
    } while ((structdirent = readdir(direntFileHandle)) != NULL);

    return returnValue;
}

static int supFS_open(const char *path, struct fuse_file_info *fileInfo) {

    int returnV = 0;
    int fileOpenRValue;

    char fullPath[PATH_MAX];

    supFS_fullpath(fullPath,path);
    fileOpenRValue = open(fullPath,fileInfo->flags);
    if(fileOpenRValue<0){
        returnV = log_error("supFS_open");
    }

    fileInfo->fh = fileOpenRValue;

    return returnV;
}

static int supFS_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {

    int returnV = 0;

    returnV = pread(fi->fh, buf, size, offset);
    if(returnV < 0){
        returnV = log_error("supFS_read");
    }

    return returnV;
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
        returnV = log_error("bb_opendir");
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
        .opendir = supFS_opendir,
        .access = supFS_access,
        .write = supFS_write,
        .rename = supFS_rename,
        .mknod = supFS_mknod,
        .mkdir = supFS_mkdir,
        .rmdir = supFS_rmdir,
        .unlink = supFS_unlink,
        .truncate = supFS_truncate,
        .chmod = supFS_chmod,
        .chown = supFS_chown,
        .utime = supFS_utime,
};

int main(int argc, char *argv[])
{
    if ((getuid() == 0) || (geteuid() == 0)) {
        printf("Root privilege forbidden, security fault.\n");
        return 1;
    }


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

    // run fuce
    fuse_return = fuse_main(argc, argv, &fuseStruct_callback, supfs_data);

    // print return value
    printf("fuse return %d\n", fuse_return);

    free(supfs_data);
    return fuse_return;
}
