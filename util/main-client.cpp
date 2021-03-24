/*
    Charon - a transport system for GSP data
    Copyright (C) 2019-2021  Autonomous Worlds Ltd

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

#include "methods.hpp"
#include "util-client.hpp"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <cstdlib>
#include <iostream>

namespace
{

DEFINE_string (server_jid, "", "Bare or full JID for the server");
DEFINE_string (backend_version, "",
               "A string identifying the version of the backend required");

DEFINE_string (client_jid, "", "Bare or full JID for the client");
DEFINE_string (password, "", "XMPP password for the client JID");

DEFINE_string (cafile, "",
               "if set, use this file as CA trust root of the system default");

DEFINE_int32 (port, 0, "Port for the local JSON-RPC server");

DEFINE_bool (waitforchange, false, "If true, enable waitforchange updates");
DEFINE_bool (waitforpendingchange, false,
             "If true, enable waitforpendingchange updates");

DEFINE_bool (detect_server, true,
             "Whether to run server detection immediately on start");

} // anonymous namespace

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);

  gflags::SetUsageMessage ("Run a Charon client");
  gflags::SetVersionString (PACKAGE_VERSION);
  gflags::ParseCommandLineFlags (&argc, &argv, true);

  try
    {
      if (FLAGS_server_jid.empty ())
        throw std::runtime_error ("--server_jid must be set");
      if (FLAGS_client_jid.empty ())
        throw std::runtime_error ("--client_jid must be set");

      if (FLAGS_port == 0)
        throw std::runtime_error ("--port must be set");

      charon::UtilClient client(FLAGS_server_jid, FLAGS_backend_version,
                                FLAGS_client_jid, FLAGS_password,
                                FLAGS_port);

      client.AddMethods (charon::GetSelectedMethods ());

      if (FLAGS_waitforchange)
        client.EnableWaitForChange ();
      if (FLAGS_waitforpendingchange)
        client.EnableWaitForPendingChange ();

      if (!FLAGS_cafile.empty ())
        client.SetRootCA (FLAGS_cafile);

      client.Run (FLAGS_detect_server);
      return EXIT_SUCCESS;
    }
  catch (const std::exception& exc)
    {
      std::cerr << "Error: " << exc.what ();
      return EXIT_FAILURE;
    }
}
