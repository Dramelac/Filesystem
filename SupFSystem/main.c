#define FUSE_USE_VERSION 26

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

static void supFS_fullpath(char fullPath[PATH_MAX],char *path){
    strcpy(fullPath, SUPFS_DATA->rootDir);//pwd = rootdir
    strncat(fullPath,path,PATH_MAX);//rootdir + shortpath
}

static int getattr_callback(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    if (strcmp(path, filepath) == 0) {
        stbuf->st_mode = S_IFREG | 0777;
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(filecontent);
        return 0;
    }

    return -ENOENT;
}

static int readdir_callback(const char *path, void *buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info *fi) {
    (void) offset;
    (void) fi;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    filler(buf, filename, NULL, 0);

    return 0;
}

static int open_callback(const char *path, struct fuse_file_info *fi) {
    return 0;
}

static int read_callback(const char *path, char *buf, size_t size, off_t offset,
                         struct fuse_file_info *fi) {

    if (strcmp(path, filepath) == 0) {
        size_t len = strlen(filecontent);
        if (offset >= len) {
            return 0;
        }

        if (offset + size > len) {
            memcpy(buf, filecontent + offset, len - offset);
            return len - offset;
        }

        memcpy(buf, filecontent + offset, size);
        return size;
    }

    return -ENOENT;
}

static struct fuse_operations fuseStruct_callback = {
        .getattr = getattr_callback,
        .open = open_callback,
        .read = read_callback,
        .readdir = readdir_callback,
};

int main(int argc, char *argv[])
{

    return fuse_main(argc, argv, &fuseStruct_callback, NULL);
}
