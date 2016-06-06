
SUBDIRS  =
SUBDIRS += src

EXTRA_DIST = \
	supFS.pc.in

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = supFS.pc

$(pkgconfig_DATA): config.status

if LINUX
package:
	echo "You can use checkinstall or some other equivalent tool to generate install package for your distribution."
endif
