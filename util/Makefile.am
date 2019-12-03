bin_PROGRAMS = charon-client charon-server

charon_client_CXXFLAGS = \
  -I$(top_srcdir)/src \
  $(JSON_CFLAGS) $(JSONRPCCLIENT_CFLAGS) $(JSONRPCSERVER_CFLAGS) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
charon_client_LDADD = \
  $(top_builddir)/src/libcharon.la \
  $(JSON_LIBS) $(JSONRPCCLIENT_LIBS) $(JSONRPCSERVER_LIBS) \
  $(GLOG_LIBS) $(GFLAGS_LIBS)
charon_client_SOURCES = client.cpp \
  methods.cpp

charon_server_CXXFLAGS = \
  -I$(top_srcdir)/src \
  $(JSON_CFLAGS) $(JSONRPCCLIENT_CFLAGS) $(JSONRPCSERVER_CFLAGS) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
charon_server_LDADD = \
  $(top_builddir)/src/libcharon.la \
  $(JSON_LIBS) $(JSONRPCCLIENT_LIBS) $(JSONRPCSERVER_LIBS) \
  $(GLOG_LIBS) $(GFLAGS_LIBS)
charon_server_SOURCES = server.cpp \
  methods.cpp

noinst_HEADERS = \
  methods.hpp
