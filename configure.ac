#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([libhugetlbfs], [2.24])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

# Checks for libraries.
AC_CHECK_LIB([dl], [main])
AC_CHECK_LIB([pthread], [main])

# glibc 2.34 removed __morecore, so it may not be available with recent versions
# Check glibc __morecore
AC_MSG_CHECKING([if glibc __morecore is available])
AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM([[
        #include <malloc.h>
        void * morecore_exists() { return &__morecore; }]])],
        [ AC_MSG_RESULT([yes])
                CFLAGS+=" -DHAS_MORECORE"],
        [ AC_MSG_RESULT([no])]
)

AC_CONFIG_FILES([Makefile
                 tests/Makefile])
AC_OUTPUT
