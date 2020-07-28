AC_INIT([charon], [0.1])
AM_INIT_AUTOMAKE

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CXX
AM_PROG_AR
AM_PATH_PYTHON([3.7])
PKG_INSTALLDIR

LT_INIT

AC_LANG([C++])
AX_CXX_COMPILE_STDCXX([14], [noext])
AX_CHECK_COMPILE_FLAG([-Wall], [CXXFLAGS="${CXXFLAGS} -Wall"])
AX_CHECK_COMPILE_FLAG([-Werror], [CXXFLAGS="${CXXFLAGS} -Werror"])
AX_CHECK_COMPILE_FLAG([-pedantic], [CXXFLAGS="${CXXFLAGS} -pedantic"])

# Windows defines ERROR, which requires us to tell glog to not define
# it as abbreviated log severity (LOG(ERROR) still works, though, and
# that is all that we actually use in the code).
# See https://hpc.nih.gov/development/glog.html.
CXXFLAGS="${CXXFLAGS} -DGLOG_NO_ABBREVIATED_SEVERITIES"

# Public dependencies exposed in the headers.
AX_PKG_CHECK_MODULES([JSON], [jsoncpp], [])
AX_PKG_CHECK_MODULES([JSONRPCCLIENT], [], [libjsonrpccpp-client])
AX_PKG_CHECK_MODULES([JSONRPCSERVER], [libjsonrpccpp-server], [])
AX_PKG_CHECK_MODULES([GLOOX], [gloox], [])

# Private dependencies for the library parts.
AX_PKG_CHECK_MODULES([GLOG], [], [libglog])

# Private dependencies for tests and binaries only.
PKG_CHECK_MODULES([GFLAGS], [gflags])
PKG_CHECK_MODULES([GTEST], [gmock gtest_main])

AC_CONFIG_FILES([
  Makefile \
  src/Makefile \
  test/Makefile \
  util/Makefile \
  \
  charon.pc
])
AC_OUTPUT

echo
echo "CXXFLAGS: ${CXXFLAGS}"
