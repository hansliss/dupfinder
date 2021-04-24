AC_INIT(dupfinder.c)
PRODUCT=dupfinder
VERSION=1.0
AC_CONFIG_HEADER(config.h)
AC_PREFIX_DEFAULT(/opt/dupfinder)
AC_PROG_CC
AC_DEFINE_UNQUOTED(PRODUCT, "$PRODUCT")
AC_DEFINE_UNQUOTED(VERSION, "$VERSION")
AC_SUBST(PRODUCT)
AC_SUBST(VERSION)

LDFLAGS="$LDFLAGS -L/usr/lib/mysql"

dnl Checks for programs.
AC_PROG_CC
if test $CC = "gcc"; then
	CFLAGS="$CFLAGS -Wall"
fi
AC_PROG_INSTALL
AC_PROG_LN_S

dnl Checks for libraries.
AC_CHECK_LIB(mysqlclient,mysql_init, ,AC_MSG_ERROR([Need libmysqlclient.a]))
AC_CHECK_LIB(crypto,MD5_Init, ,AC_MSG_ERROR([Need OpenSSLs libcrypto.a]))

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(unistd.h mysql/mysql.h openssl/md5.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

dnl Checks for library functions.
AC_TYPE_SIGNAL

AC_OUTPUT(Makefile)
