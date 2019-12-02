/*
    Charon - a transport system for GSP data
    Copyright (C) 2019  Autonomous Worlds Ltd

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "config.h"

#include "rpcserver.hpp"
#include "server.hpp"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <thread>

namespace
{

DEFINE_string (backend_rpc_url, "",
               "URL at which the backend JSON-RPC interface is available");
DEFINE_string (methods, "", "Comma-separated list of methods to forward");

DEFINE_string (server_jid, "", "Bare or full JID for the server");
DEFINE_string (password, "", "XMPP password for the server JID");
DEFINE_int32 (priority, 0, "Priority for the XMPP connection");

} // anonymous namespace

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);

  gflags::SetUsageMessage ("Run a Charon server");
  gflags::SetVersionString (PACKAGE_VERSION);
  gflags::ParseCommandLineFlags (&argc, &argv, true);

  if (FLAGS_backend_rpc_url.empty ())
    {
      std::cerr << "Error: --backend_rpc_url must be set" << std::endl;
      return EXIT_FAILURE;
    }
  if (FLAGS_server_jid.empty ())
    {
      std::cerr << "Error: --server_jid must be set" << std::endl;
      return EXIT_FAILURE;
    }
  if (FLAGS_methods.empty ())
    {
      std::cerr << "Error: no allowed --methods are set" << std::endl;
      return EXIT_FAILURE;
    }

  charon::ForwardingRpcServer backend(FLAGS_backend_rpc_url);
  LOG (INFO)
      << "Forwarding calls to JSON-RPC server at " << FLAGS_backend_rpc_url;
  std::istringstream methodsIn(FLAGS_methods);
  while (methodsIn.good ())
    {
      std::string method;
      std::getline (methodsIn, method, ',');
      LOG (INFO) << "Allowing method: " << method;
      backend.AllowMethod (method);
    }

  LOG (INFO) << "Connecting server to XMPP as " << FLAGS_server_jid;
  charon::Server srv(backend);
  srv.Connect (FLAGS_server_jid, FLAGS_password, FLAGS_priority);

  while (true)
    std::this_thread::sleep_for (std::chrono::seconds (1));

  return EXIT_SUCCESS;
}
