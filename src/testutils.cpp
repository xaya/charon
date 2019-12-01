// Copyright (C) 2019 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "testutils.hpp"

#include <glog/logging.h>

#include <sstream>

namespace charon
{

const char* const XMPP_SERVER = "chat.xaya.io";

/**
 * Our test accounts.  They are all set up for XID on mainnet, and the address
 * CLkoEc3g1XCqF1yevLfE1F2EksLhGSd8GC is set as global signer.  It has the
 * private key LMeJqBHefZdZbH7pHBqhmBu3pFzBpVo78SnBgsoHc6KYaDv9CEYp.
 * The passwords given are unexpiring XID credentials for chat.xaya.io.
 */
const TestAccount ACCOUNTS[] =
  {
    {
      "xmpptest1",
      "CkEfa5+WT2Rc5/TiMDhMynAbSJ+DY9FmE5lcWgWMRQWUBV5UQsgjiBWL302N4kdLZYygJVBV"
      "x3vYsDNUx8xBbw27WA==",
    },
    {
      "xmpptest2",
      "CkEgOEFNwRdLQ6uD543MJLSzip7mTahM1we9GDl3S5NlR49nrJ0JxcFfQmDbbF4C4OpqSlTp"
      "x8OG6xtFjCUMLh/AGA==",
    },
  };

gloox::JID
JIDWithoutResource (const TestAccount& acc)
{
  gloox::JID res;
  res.setUsername (acc.name);
  res.setServer (XMPP_SERVER);
  return res;
}

gloox::JID
JIDWithResource (const TestAccount& acc, const std::string& res)
{
  gloox::JID jid = JIDWithoutResource (acc);
  jid.setResource (res);
  return jid;
}

Json::Value
ParseJson (const std::string& str)
{
  std::istringstream in(str);
  Json::Value res;
  in >> res;
  return res;
}

Json::Value
TestBackend::HandleMethod (const std::string& method, const Json::Value& params)
{
  CHECK (params.isArray ());
  CHECK_EQ (params.size (), 1);
  CHECK (params[0].isString ());

  if (method == "echo")
    return params[0];

  if (method == "error")
    throw Error (42, params[0].asString (), Json::Value ());

  LOG (FATAL) << "Unexpected method: " << method;
}

} // namespace charon
