
DEVELFLAGS =

bin_PROGRAMS = \
	supFS \
	supFS.probe

if LINUX
moddir = $(libdir)/umview/modules
mod_LTLIBRARIES = umfuseext2.la
endif

supFS_probe_SOURCES = \
	main.c \
    fs_method.c \
    main.h \
	SupFS_log.h \
	SupFS_log.c

supFS_probe_CFLAGS = \
	-Wall \
	-DHAVE_CONFIG_H \
	-I/usr/local/include \
    -D_FILE_OFFSET_BITS=64

supFS_SOURCES =	\
	main.c \
    fs_method.c \
    main.h \
    SupFS_log.c \
    SupFS_log.h

umfuseext2_la_SOURCES = \
    main.c \
    fs_method.c \
    main.h \
    SupFS_log.c \
    SupFS_log.h

umfuseext2_la_CFLAGS = \
	-Wall \
	-DHAVE_CONFIG_H \
	-D_GNU_SOURCE \
	$(DEVELFLAGS) \
	-I$(includedir)/umview \
	-I/usr/local/include \
    -D_FILE_OFFSET_BITS=64

umfuseext2_la_LDFLAGS = \
	-module \
	-avoid-version \
	-export-dynamic \
	-lext2fs

supFS_CFLAGS = \
	-Wall \
	-DHAVE_CONFIG_H \
	-I/usr/local/include \
	-D_FILE_OFFSET_BITS=64

if LINUX
install-data-hook:
	cd "$(DESTDIR)/$(moddir)" && rm -f $(mod_LTLIBRARIES)

install-exec-local:
	$(INSTALL) -d "$(DESTDIR)/usr/sbin"
	$(LN_S) -f "../bin/supFS" "$(DESTDIR)/usr/sbin/mount.supFS"
endif
