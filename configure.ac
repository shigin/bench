AC_INIT([bench], [0.01])
AM_INIT_AUTOMAKE
AC_CONFIG_FILES([Makefile])
AC_PROG_CC
AC_PROG_INSTALL
AC_CHECK_LIB(m, sqrt, [], [AC_MSG_ERROR("need sqrt")])
AC_CHECK_HEADERS([paths.h])
AC_CONFIG_HEADERS(config.h:config.hin)
AC_OUTPUT()
