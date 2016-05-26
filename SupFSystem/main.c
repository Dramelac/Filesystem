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
    FILE *file;
    char *rootDir;
};
#define SUPFS_DATA ((struct supFS_state *) fuse_get_context()->private_data)

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
                         off_t offset, struct fuse_file_info *fileInfo) {

    int returnValue = 0;
    DIR *direntFileHandle;
    struct dirent *structdirent;


    direntFileHandle = (DIR *) (uintptr_t) fileInfo->fh;

    structdirent = readdir(direntFileHandle);

    if (structdirent <= 0) {
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


    return returnV;
}

static int supFS_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fileInfo) {

    int returnV = 0;

    returnV = pread(fileInfo->fh, buf, size, offset);
    if(returnV<0){
        returnV = log_error("supFS_read");
    }

    return returnV;
}

static struct fuse_operations fuseStruct_callback = {
        .getattr = supFS_getattr,
        .open = supFS_open,
        .read = supFS_read,
        .readdir = supFS_readdir,
};

int main(int argc, char *argv[])
{

    return fuse_main(argc, argv, &fuseStruct_callback, NULL);
}
