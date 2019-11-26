lib_LTLIBRARIES = libcharon.la
charondir = $(includedir)/charon

libcharon_la_CXXFLAGS = \
  $(JSON_CFLAGS) \
  $(GLOG_CFLAGS) $(GLOOX_CFLAGS)
libcharon_la_LIBADD = \
  $(JSON_LIBS) \
  $(GLOG_LIBS) $(GLOOX_LIBS)
libcharon_la_SOURCES = \
  stanzas.cpp
noinst_HEADERS = \
  private/stanzas.hpp

check_PROGRAMS = tests
TESTS = tests

tests_CXXFLAGS = \
  $(GTEST_CFLAGS) $(GLOG_CFLAGS)
tests_LDADD = \
  $(builddir)/libcharon.la \
  $(GTEST_LIBS) $(GLOG_LIBS)
tests_SOURCES = \
  stanzas_tests.cpp
