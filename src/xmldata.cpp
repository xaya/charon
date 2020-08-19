/*
    Charon - a transport system for GSP data
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#include "xmldata.hpp"

#include <glog/logging.h>

#include <sstream>

namespace charon
{

namespace
{

/**
 * Returns true if the given string is not considered "binary".  In particular,
 * this means that it consists only of printable ASCII characters and newlines.
 *
 * Strings like this will be encoded with <raw> and won't need base64 encoding.
 * Serialised JSON in particular fulfills this.
 */
bool
CanStoreRaw (const std::string& str)
{
  for (const char c : str)
    {
      if (c == '\n')
        continue;
      if (c >= ' ')
        {
          CHECK_LT (static_cast<unsigned char> (c), 0x80);
          continue;
        }

      VLOG (2) << "Not a raw-able character: " << static_cast<int> (c);
      return false;
    }

  return true;
}

/**
 * Tries to decode the payload in a particular payload tag.
 */
bool
DecodePayloadTag (const gloox::Tag& tag, std::string& payload)
{
  if (tag.name () == "raw")
    {
      payload = tag.cdata ();
      return true;
    }

  LOG (WARNING) << "Invalid payload tag: " << tag.name ();
  return false;
}

} // anonymous namespace

std::unique_ptr<gloox::Tag>
EncodeXmlPayload (const std::string& name, const std::string& payload)
{
  auto res = std::make_unique<gloox::Tag> (name);

  CHECK (CanStoreRaw (payload)) << "Non-raw payloads are not yet supported";
  if (!payload.empty ())
    res->addChild (new gloox::Tag ("raw", payload));

  return res;
}

bool
DecodeXmlPayload (const gloox::Tag& tag, std::string& payload)
{
  std::ostringstream res;
  for (const auto* child : tag.children ())
    {
      std::string cur;
      if (!DecodePayloadTag (*child, cur))
        return false;
      res << cur;
    }

  payload = res.str ();
  return true;
}

std::unique_ptr<gloox::Tag>
EncodeXmlJson (const std::string& name, const Json::Value& val)
{
  Json::StreamWriterBuilder wbuilder;
  wbuilder["commentStyle"] = "None";
  wbuilder["indentation"] = "";
  wbuilder["enableYAMLCompatibility"] = false;
  wbuilder["dropNullPlaceholders"] = false;
  wbuilder["useSpecialFloats"] = false;

  return EncodeXmlPayload (name, Json::writeString (wbuilder, val));
}

bool
DecodeXmlJson (const gloox::Tag& tag, Json::Value& val)
{
  std::string serialised;
  if (!DecodeXmlPayload (tag, serialised))
    return false;

  Json::CharReaderBuilder rbuilder;
  rbuilder["allowComments"] = false;
  rbuilder["strictRoot"] = false;
  rbuilder["failIfExtra"] = true;
  rbuilder["rejectDupKeys"] = true;

  std::istringstream in(serialised);
  std::string parseErrs;
  if (!Json::parseFromStream (rbuilder, in, &val, &parseErrs))
    {
      LOG (WARNING)
          << "Failed parsing JSON:\n"
          << serialised << "\n" << parseErrs;
      return false;
    }

  return true;
}

} // namespace charon
