dnl vim: set sw=8 sts=8 noet :

AC_INIT([ktsuss], [2], [dcortarello@gmail.com])
AC_PREREQ(2.5)
AM_INIT_AUTOMAKE(1.9)
AC_PROG_CC
AC_CONFIG_HEADER(config.h)
AC_PROG_CC_C_O
dnl check for required programs
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_RANLIB
AC_PROG_MAKE_SET
AC_TYPE_SIZE_T
AC_TYPE_PID_T
AC_C_BIGENDIAN
AC_C_INLINE
AC_CHECK_SIZEOF(int, 32)
PKG_CHECK_MODULES(DEPS, gtk+-2.0 >= 2.2 glib-2.0 >= 2.2)
AC_SUBST(DEPS_CFLAGS)
AC_SUBST(DEPS_LIBS)
AC_ARG_ENABLE([sudo], [  --enable-sudo=yes|no use the sudo backend instead the su one. default no.], [BUILD_SUDO="$enableval"], [BUILD_SUDO=no])
case $BUILD_SUDO in
	no)
		AC_MSG_CHECKING([whether we can locate the su program])
		supath=`which su 2>/dev/null`
		if test "x$supath" = "x"; then
			echo
			AC_MSG_ERROR([Could not find su in PATH])
		else
			AC_DEFINE_UNQUOTED([SUPATH], "$supath", [su path])
			echo $supath
		fi
	;;
	yes)
		AC_MSG_CHECKING([whether we can locate the sudo program])
		sudopath=`which sudo 2>/dev/null`
		if test "x$sudopath" = "x"; then
			echo
			AC_MSG_ERROR([Could not find sudo in PATH])
		else
			AC_DEFINE_UNQUOTED([SUDOPATH], "$sudopath", [sudo path])
			echo $sudopath
		fi
	;;
esac
AM_CONDITIONAL(BUILD_WITH_SUDO, test "x$BUILD_SUDO" = "xyes")

dnl output
AC_OUTPUT(
	Makefile
	src/Makefile
	pixmaps/Makefile
	)
