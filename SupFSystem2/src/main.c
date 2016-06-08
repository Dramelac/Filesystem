#include "main.h"

static int parse_options (int argc, char *argv[], struct supFs_data *opts)
{

    if (argc != 3) {
        log_error("error arg number, please specify device and mount point");
        return -1;
    }

    opts->device = argv[1];
    opts->mnt_point = argv[2];

    if (!opts->device) {
        log_error("error device");
        return -1;
    }
    if (!opts->mnt_point) {
        log_error("error mountpoint");
        return -1;
    }

    return 0;
}

static struct fuse_operations fuseStruct_callback = {
        .getattr        = supFS_getattr,
        .mknod          = op_mknod,
        .mkdir          = op_mkdir,
        .unlink         = op_unlink,
        .rmdir          = op_rmdir,
        .rename         = op_rename,
        .truncate       = op_truncate,
        .open           = supFS_open,
        .read           = supFS_read,
        .write          = op_write,
        .flush          = op_flush,
        .release	= op_release,
        .opendir        = supFS_open,
        .readdir        = op_readdir,
        .releasedir     = op_release,
        .init		= op_init,
        .destroy	= op_destroy,
        .access         = check_access,
        .create         = op_create,
};

int main (int argc, char *argv[])
{
    int returnValue = 0;
    struct fuse_args fargs = FUSE_ARGS_INIT(0, NULL);

    struct supFs_data dataOptionsStruct;
    memset(&dataOptionsStruct, 0, sizeof(dataOptionsStruct));

    char msg[100];
    snprintf(msg, sizeof(msg), "start with fuse_version: %d", fuse_version());
    log_info(msg);

    // check and load option in dataStruct
    if (parse_options(argc, argv, &dataOptionsStruct)) {
        printf("Usage: <device> <mount_point> [-o option[,...]]\n");
        return -1;
    }

    // create fuse options
    char parsed_options[255] = "rw,debug,allow_other,default_permissions,fsname=";
    strcat(parsed_options, dataOptionsStruct.device);

    snprintf(msg, sizeof(msg), "dataOptionsStruct.device: %s", dataOptionsStruct.device);
    log_info(msg);
    snprintf(msg, sizeof(msg), "dataOptionsStruct.mnt_point: %s", dataOptionsStruct.mnt_point);
    log_info(msg);
    snprintf(msg, sizeof(msg), "parsed_options: %s", parsed_options);
    log_info(msg);

    fuse_opt_add_arg(&fargs, "-s");
    fuse_opt_add_arg(&fargs, "-o");
    fuse_opt_add_arg(&fargs, parsed_options);
    fuse_opt_add_arg(&fargs, dataOptionsStruct.mnt_point);

    returnValue = fuse_main(fargs.argc, fargs.argv, &fuseStruct_callback, &dataOptionsStruct);

    log_info("Exit fuse FS");

    fuse_opt_free_args(&fargs);
    return returnValue;
}
