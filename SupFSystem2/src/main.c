#include "main.h"

static const char *HOME = "http://github.com/alperakcan/fuse-ext2/";

#if __FreeBSD__ == 10
static char def_opts[] = "allow_other,default_permissions,local,";
static char def_opts_rd[] = "noappledouble,";
#else
static char def_opts[] = "allow_other,default_permissions,";
static char def_opts_rd[] = "";
#endif

static const char *usage_msg =
        "\n"
                "%s %s %d - FUSE EXT2FS Driver\n"
                "\n"
                "Copyright (C) 2008-2015 Alper Akcan <alper.akcan@gmail.com>\n"
                "Copyright (C) 2009 Renzo Davoli <renzo@cs.unibo.it>\n"
                "\n"
                "Usage:    %s <device|image_file> <mount_point> [-o option[,...]]\n"
                "\n"
                "Options:  ro, force, allow_others\n"
                "          Please see details in the manual.\n"
                "\n"
                "Example:  fuse-ext2 /dev/sda1 /mnt/sda1\n"
                "\n"
                "%s\n"
                "\n";

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

static void usage (void)
{
    printf(usage_msg, PACKAGE, VERSION, fuse_version(), PACKAGE, HOME);
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

#if 0
    printf("arguments;\n");
	for (c = 0; c < argc; c++) {
		printf("%d: %s\n", c, argv[c]);
	}
	printf("done\n");
#endif

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
            case 'v':
                /*
                 * We must handle the 'verbose' option even if
                 * we don't use it because mount(8) passes it.
                 */
                opts->debug = 1;
                break;
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

static char * parse_mount_options (const char *orig_opts, struct supFs_data *opts)
{
    char *options, *s, *opt, *val, *ret;

    ret = malloc(strlen(def_opts) + strlen(def_opts_rd) + strlen(orig_opts) + 256 + PATH_MAX);
    if (!ret) {
        return NULL;
    }

    *ret = 0;
    options = strdup(orig_opts);
    if (!options) {
        log_error("strdup failed");
        return NULL;
    }

    s = options;
    while (s && *s && (val = strsep(&s, ","))) {
        opt = strsep(&val, "=");
        if (!strcmp(opt, "ro")) { /* Read-only mount. */
            if (val) {
                log_error("'ro' option should not have value");
                free(ret);
                ret = NULL;
            }
            opts->readonly = 1;
            strcat(ret, "ro,");
        } else if (!strcmp(opt, "rw")) { /* Read-write mount */
            if (val) {
                log_error("'rw' option should not have value");
                free(ret);
                ret = NULL;
            }
            opts->readonly = 0;
            strcat(ret, "rw,");
        } else if (!strcmp(opt, "rw+")) { /* Read-write mount */
            if (val) {
                log_error("'rw+' option should not have value");
                free(ret);
                ret = NULL;
            }
            opts->readonly = 0;
            opts->force = 1;
            strcat(ret, "rw,");
        } else if (!strcmp(opt, "debug")) { /* enable debug */
            if (val) {
                log_error("'debug' option should not have value");
                free(ret);
                ret = NULL;
            }
            opts->debug = 1;
            strcat(ret, "debug,");
        } else if (!strcmp(opt, "silent")) { /* keep silent */
            if (val) {
                log_error("'silent' option should not have value");
                free(ret);
                ret = NULL;
            }
            opts->silent = 1;
        } else if (!strcmp(opt, "force")) { /* enable read/write */
            if (val) {
                log_error("'force option should no have value");
                free(ret);
                ret = NULL;
            }
            opts->force = 1;
        } else { /* Probably FUSE option. */
            strcat(ret, opt);
            if (val) {
                strcat(ret, "=");
                strcat(ret, val);
            }
            strcat(ret, ",");
        }
    }

    if (opts->readonly == 0 && opts->force == 0) {
        fprintf(stderr, "Mounting %s Read-Only.\nUse \'force\' or \'rw+\' options to enable Read-Write mode\n",opts->device);
        opts->readonly = 1;
    }

    strcat(ret, def_opts);
    if (opts->readonly == 1) {
        strcat(ret, def_opts_rd);
        strcat(ret, "ro,");
    }
    strcat(ret, "fsname=");
    strcat(ret, opts->device);

    free(options);
    return ret;
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
    struct stat sbuf;
    char *parsed_options = NULL;
    struct fuse_args fargs = FUSE_ARGS_INIT(0, NULL);
    struct supFs_data dataOptionsStruct;

    char msg[100];
    snprintf(msg, sizeof(msg), "version:'%s', fuse_version:'%d / %d / %d'", VERSION, FUSE_USE_VERSION,  FUSE_VERSION, fuse_version());
    log_info(msg);

    memset(&dataOptionsStruct, 0, sizeof(dataOptionsStruct));

    if (parse_options(argc, argv, &dataOptionsStruct)) {
        usage();
        return -1;
    }

    if (stat(dataOptionsStruct.device, &sbuf)) {
        snprintf(msg, sizeof(msg), "Failed to access '%s'", dataOptionsStruct.device);
        log_error(msg);
        returnValue = -3;
    }
    if (do_probe(&dataOptionsStruct) != 0) {
        log_error("Probe failed");
        returnValue = -4;
    }

    parsed_options = parse_mount_options(dataOptionsStruct.options ? dataOptionsStruct.options : "", &dataOptionsStruct);
    if (!parsed_options) {
        returnValue = -2;
    }

    snprintf(msg, sizeof(msg), "dataOptionsStruct.device: %s", dataOptionsStruct.device);
    log_info(msg);
    snprintf(msg, sizeof(msg), "dataOptionsStruct.mnt_point: %s", dataOptionsStruct.mnt_point);
    log_info(msg);
    snprintf(msg, sizeof(msg), "dataOptionsStruct.volname: %s", (dataOptionsStruct.volname != NULL) ? dataOptionsStruct.volname : "");
    log_info(msg);
    snprintf(msg, sizeof(msg), "dataOptionsStruct.options: %s", dataOptionsStruct.options);
    log_info(msg);
    snprintf(msg, sizeof(msg), "parsed_options: %s", parsed_options);
    log_info(msg);

    if (fuse_opt_add_arg(&fargs, PACKAGE) == -1 ||
        fuse_opt_add_arg(&fargs, "-s") == -1 ||
        fuse_opt_add_arg(&fargs, "-o") == -1 ||
        fuse_opt_add_arg(&fargs, parsed_options) == -1 ||
        fuse_opt_add_arg(&fargs, dataOptionsStruct.mnt_point) == -1) {
        log_error("Failed to set FUSE options");
        fuse_opt_free_args(&fargs);
        returnValue = -5;
    }

    if (dataOptionsStruct.readonly == 0) {
        log_info("mounting read-write");
    } else {
        log_info("mounting read-only");
    }

    if (returnValue == 0) {
        returnValue = fuse_main(fargs.argc, fargs.argv, &fuseStruct_callback, &dataOptionsStruct);
    }

    log_info("Exit fuse FS");

    fuse_opt_free_args(&fargs);
    free(parsed_options);
    free(dataOptionsStruct.options);
    free(dataOptionsStruct.device);
    free(dataOptionsStruct.volname);
    return returnValue;
}
