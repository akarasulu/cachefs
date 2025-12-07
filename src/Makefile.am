## Process this file with automake to produce Makefile.in

bin_PROGRAMS = cachefs

if HAVE_SQLITE3
noinst_HEADERS = debug.h permchain.h userinfo.h arena.h misc.h usermap.h rate_limiter.h \
                 cache_meta.h cache_block.h cache_coherency.h
cachefs_SOURCES = cachefs.c debug.c permchain.c userinfo.c arena.c misc.c usermap.c rate_limiter.c \
                 cache_meta.c cache_block.c cache_coherency.c
else
noinst_HEADERS = debug.h permchain.h userinfo.h arena.h misc.h usermap.h rate_limiter.h
cachefs_SOURCES = cachefs.c debug.c permchain.c userinfo.c arena.c misc.c usermap.c rate_limiter.c
endif

AM_CPPFLAGS = ${my_CPPFLAGS} ${fuse_CFLAGS} ${fuse3_CFLAGS} ${fuse_t_CFLAGS} ${SQLITE3_CFLAGS}
AM_CFLAGS = ${my_CFLAGS}
cachefs_LDADD = ${fuse_LIBS} ${fuse3_LIBS} ${fuse_t_LIBS} ${SQLITE3_LIBS} ${my_LDFLAGS}

man_MANS = cachefs.1

if INSTALL_MACOS_FS_LINK
cachefs_BUNDLEDIR = $(DESTDIR)/Library/Filesystems/cachefs.fs
cachefs_BUNDLEBINDIR = "${cachefs_BUNDLEDIR}/Contents/Resources"

install-exec-hook:
	(mkdir -p "${cachefs_BUNDLEBINDIR}"; ln -sf "${bindir}/cachefs" "${cachefs_BUNDLEBINDIR}/mount_cachefs")

uninstall-hook:
	(rm -rf ${cachefs_BUNDLEDIR})
endif
