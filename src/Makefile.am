bin_PROGRAMS = ktsuss

ktsuss_SOURCES = ktsuss.c su_backend.c sudo_backend.c
ktsuss_LDADD = $(DEPS_LIBS) -lutil
AM_CPPFLAGS = $(DEPS_CFLAGS) -DPIXMAPS=\""$(datadir)/pixmaps"\"
