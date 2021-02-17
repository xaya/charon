noinst_LTLIBRARIES = libutils.la
bin_PROGRAMS = charon-client charon-server

libutils_la_CXXFLAGS = \
  -I$(top_srcdir)/src \
  $(JSON_CFLAGS) $(JSONRPCCLIENT_CFLAGS) $(JSONRPCSERVER_CFLAGS) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
libutils_la_LIBADD = \
  $(top_builddir)/src/libcharon.la \
  $(JSON_LIBS) $(JSONRPCCLIENT_LIBS) $(JSONRPCSERVER_LIBS) \
  $(GLOG_LIBS) $(GFLAGS_LIBS)
libutils_la_SOURCES = \
  methods.cpp
noinst_HEADERS = \
  methods.hpp

charon_client_CXXFLAGS = \
  -I$(top_srcdir)/src \
  $(JSON_CFLAGS) $(JSONRPCCLIENT_CFLAGS) $(JSONRPCSERVER_CFLAGS) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
charon_client_LDADD = \
  $(builddir)/libutils.la \
  $(top_builddir)/src/libcharon.la \
  $(JSON_LIBS) $(JSONRPCCLIENT_LIBS) $(JSONRPCSERVER_LIBS) \
  $(GLOG_LIBS) $(GFLAGS_LIBS)
charon_client_SOURCES = main-client.cpp

charon_server_CXXFLAGS = \
  -I$(top_srcdir)/src \
  $(JSON_CFLAGS) $(JSONRPCCLIENT_CFLAGS) $(JSONRPCSERVER_CFLAGS) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
charon_server_LDADD = \
  $(builddir)/libutils.la \
  $(top_builddir)/src/libcharon.la \
  $(JSON_LIBS) $(JSONRPCCLIENT_LIBS) $(JSONRPCSERVER_LIBS) \
  $(GLOG_LIBS) $(GFLAGS_LIBS)
charon_server_SOURCES = main-server.cpp
