#include "main.h"


static void usage (void)
{
    printf("Usage: <device> <mount_point> [-o option[,...]]\n");
}


static int strappend (char **dest, const char *append)
{
    char *p;
    size_t size;

    if (!dest) {
        return -1;
    }
    if (!append) {
        return 0;
    }

    size = strlen(append) + 1;
    if (*dest) {
        size += strlen(*dest);
    }

    p = realloc(*dest, size);
    if (!p) {
        debugf_main("Memory realloction failed");
        return -1;
    }

    if (*dest) {
        strcat(p, append);
    } else {
        strcpy(p, append);
    }
    *dest = p;

    return 0;
}

static int parse_options (int argc, char *argv[], struct supFs_data *opts)
{
    int c;

    static const char *sopt = "o:hv";
    static const struct option lopt[] = {
            { "options",	 required_argument,	NULL, 'o' },
            { "help",	 no_argument,		NULL, 'h' },
            { "verbose",	 no_argument,		NULL, 'v' },
            { NULL,		 0,			NULL,  0  }
    };

    opterr = 0; /* We'll handle the errors, thank you. */

    while ((c = getopt_long(argc, argv, sopt, lopt, NULL)) != -1) {
        switch (c) {
            case 'o':
                if (opts->options)
                if (strappend(&opts->options, ","))
                    return -1;
                if (strappend(&opts->options, optarg))
                    return -1;
                break;
            case 'h':
                usage();
                exit(9);
            default:
                log_error("Unknown options");
                return -1;
        }
    }

    if (optind < argc) {
        optarg=argv[optind++];
        if (optarg[0] != '/') {
            char fulldevice[PATH_MAX+1];
            if (!realpath(optarg, fulldevice)) {
                log_error("Cannot mount device");
                free(opts->device);
                opts->device = NULL;
                return -1;
            } else
                opts->device = strdup(fulldevice);
        } else
            opts->device = strdup(optarg);
    }

    if (optind < argc) {
        opts->mnt_point = argv[optind++];
    }

    if (optind < argc) {
        log_error("error arg number, please specify device and mount point");
        return -1;
    }

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
        .getattr        = op_getattr,
        .mknod          = op_mknod,
        .mkdir          = op_mkdir,
        .unlink         = op_unlink,
        .rmdir          = op_rmdir,
        .rename         = op_rename,
        .truncate       = op_truncate,
        .open           = op_open,
        .read           = op_read,
        .write          = op_write,
        .flush          = op_flush,
        .release	= op_release,
        .opendir        = op_open,
        .readdir        = op_readdir,
        .releasedir     = op_release,
        .init		= op_init,
        .destroy	= op_destroy,
        .access         = op_access,
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
        usage();
        return -1;
    }

    // create fuse options
    char parsed_options[255] = "rw,debug,allow_other,default_permissions,fsname=";
    strcat(parsed_options, dataOptionsStruct.device);

    snprintf(msg, sizeof(msg), "dataOptionsStruct.device: %s", dataOptionsStruct.device);
    log_info(msg);
    snprintf(msg, sizeof(msg), "dataOptionsStruct.mnt_point: %s", dataOptionsStruct.mnt_point);
    log_info(msg);
    snprintf(msg, sizeof(msg), "dataOptionsStruct.options: %s", dataOptionsStruct.options);
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
    free(dataOptionsStruct.options);
    free(dataOptionsStruct.device);
    return returnValue;
}
