lib_LTLIBRARIES = libcharon.la
charondir = $(includedir)/charon

EXTRA_DIST = rpc-stubs/testbackend.json

RPC_STUBS = \
  rpc-stubs/testbackendserverstub.h
BUILT_SOURCES = $(RPC_STUBS)
CLEANFILES = $(RPC_STUBS)

libcharon_la_CXXFLAGS = \
  $(JSON_CFLAGS) $(JSONRPCCLIENT_CFLAGS) $(JSONRPCSERVER_CFLAGS) \
  $(GLOG_CFLAGS) $(GLOOX_CFLAGS)
libcharon_la_LIBADD = \
  $(JSON_LIBS) $(JSONRPCCLIENT_LIBS) $(JSONRPCSERVER_LIBS) \
  $(GLOG_LIBS) $(GLOOX_LIBS)
libcharon_la_SOURCES = \
  client.cpp \
  notifications.cpp \
  pubsub.cpp \
  rpcserver.cpp \
  server.cpp \
  stanzas.cpp \
  waiterthread.cpp \
  xmppclient.cpp
charon_HEADERS = \
  client.hpp \
  notifications.hpp \
  rpcserver.hpp \
  server.hpp \
  waiterthread.hpp
noinst_HEADERS = \
  private/pubsub.hpp \
  private/stanzas.hpp \
  private/xmppclient.hpp

check_PROGRAMS = tests
TESTS = tests

tests_CXXFLAGS = \
  $(JSON_CFLAGS) $(JSONRPCCLIENT_CFLAGS) $(JSONRPCSERVER_CFLAGS) \
  $(GTEST_CFLAGS) $(GLOG_CFLAGS) $(GLOOX_CFLAGS)
tests_LDADD = \
  $(builddir)/libcharon.la \
  $(JSON_LIBS) $(JSONRPCCLIENT_LIBS) $(JSONRPCSERVER_LIBS) \
  $(GTEST_LIBS) $(GLOG_LIBS) $(GLOOX_LIBS)
tests_SOURCES = \
  testutils.cpp \
  \
  client_tests.cpp \
  pubsub_tests.cpp \
  rpcserver_tests.cpp \
  server_tests.cpp \
  stanzas_tests.cpp \
  waiterthread_tests.cpp \
  xmppclient_tests.cpp
check_HEADERS = \
  testutils.hpp \
  rpc-stubs/testbackendserverstub.h

rpc-stubs/testbackendserverstub.h: $(srcdir)/rpc-stubs/testbackend.json
	jsonrpcstub "$<" \
          --cpp-server=TestBackendServerStub \
          --cpp-server-file="$@"
