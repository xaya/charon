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

#ifndef CHARON_TESTUTILS_HPP
#define CHARON_TESTUTILS_HPP

#include <gloox/jid.h>

#include <json/json.h>

#include <string>

namespace charon
{

/** XMPP server used for testing.  */
extern const char* const XMPP_SERVER;

/**
 * Data for one of the test accounts that we use.
 */
struct TestAccount
{

  /** The username for the XMPP server.  */
  const char* name;

  /** The password for logging into the server.  */
  const char* password;

};

/** Test accounts on the server.  */
extern const TestAccount ACCOUNTS[2];

/**
 * Constructs the JID for a test account, without resource.
 */
gloox::JID JIDWithoutResource (const TestAccount& acc);

/**
 * Constructs the JID for a test account with the given resource.
 */
gloox::JID JIDWithResource (const TestAccount& acc, const std::string& res);

/**
 * Parses a string as JSON (for use in test data).
 */
Json::Value ParseJson (const std::string& str);

} // namespace charon

#endif // CHARON_TESTUTILS_HPP
