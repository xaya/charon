#include "methods.hpp"
#include "util-client.hpp"

#include <glog/logging.h>

#include <string>

bool
RunServer (const std::string& serverJid, const std::string& backendVersion,
           const std::string& clientJid, const std::string& password,
           const int port, const std::string& methodsFile)
{
  if (serverJid.empty () || clientJid.empty () || port == 0)
    return false;

  charon::UtilClient client(serverJid, backendVersion,
                            clientJid, password,
                            port);

  client.AddMethods (charon::GetMethodsFromJsonSpec (methodsFile));

  client.EnableWaitForChange ();
  client.EnableWaitForPendingChange ();

  try
    {
      client.Run (true);
      return true;
    }
  catch (const std::exception& exc)
    {
      LOG (ERROR) << "Error: " << exc.what ();
      return false;
    }
}
