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

#include "xmldata_internal.hpp"

#include <openssl/evp.h>

#include <glog/logging.h>

#include <cstddef>
#include <sstream>
#include <vector>

namespace charon
{

namespace
{

/* ************************************************************************** */

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
 * Encodes a given binary string as base64.
 */
std::string
EncodeBase64 (const std::string& data)
{
  /* We need an upper bound on the length of the generated data, so that we
     can reserve a buffer large enough.  The output will be four bytes for
     every three in the input, plus newlines every 64 bytes, plus one NUL at
     the end.  By doubling the input data plus some extra bytes in the case
     of very short input, we are certainly above that.  */
  const size_t bufSize = 3 + 2 * data.size ();

  std::vector<unsigned char> encoded(bufSize, 0);
  const int n
      = EVP_EncodeBlock (encoded.data (),
                         reinterpret_cast<const unsigned char*> (data.data ()),
                         data.size ());
  CHECK_LE (n + 1, bufSize);

  /* Strip out all newline characters from the generated string.  */
  std::ostringstream res;
  for (int i = 0; i < n; ++i)
    if (encoded[i] != '\n')
      res << encoded[i];

  return res.str ();
}

/**
 * Tries to decode a given base64 string.
 */
bool
DecodeBase64 (const std::string& encoded, std::string& data)
{
  /* Check for the number of padding characters.  */
  size_t paddings = 0;
  for (const char c : encoded)
    switch (c)
      {
      case '=':
        ++paddings;
        continue;

      case ' ':
      case '\t':
      case '\n':
      case '\v':
      case '\f':
      case '\r':
        continue;

      default:
        if (paddings > 0)
          {
            LOG (WARNING) << "Padding in the middle of base64 data";
            return false;
          }
        break;
      }
  if (paddings > 3)
    {
      LOG (WARNING) << "Too many padding characters detected: " << paddings;
      return false;
    }

  /* The output data will never be longer than the input.  If the input
     contains whitespace (which is ignored), the difference will be even
     more pronounced.  */
  const size_t bufSize = encoded.size ();
  data.resize (bufSize);

  const unsigned char* in
      = reinterpret_cast<const unsigned char*> (encoded.data ());
  unsigned char* out = reinterpret_cast<unsigned char*> (&data[0]);
  const int n = EVP_DecodeBlock (out, in, encoded.size ());
  if (n == -1)
    {
      LOG (WARNING) << "OpenSSL base64 decode returned error";
      return false;
    }
  CHECK_LE (n, bufSize);

  CHECK_LE (paddings, n);
  data.resize (n - paddings);

  return true;
}

/* ************************************************************************** */

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

  if (tag.name () == "base64")
    return DecodeBase64 (tag.cdata (), payload);

  LOG (WARNING) << "Invalid payload tag: " << tag.name ();
  return false;
}

/* ************************************************************************** */

} // anonymous namespace

std::unique_ptr<gloox::Tag>
EncodeXmlBase64 (const std::string& payload)
{
  return std::make_unique<gloox::Tag> ("base64", EncodeBase64 (payload));
}

std::unique_ptr<gloox::Tag>
EncodeXmlPayload (const std::string& name, const std::string& payload)
{
  auto res = std::make_unique<gloox::Tag> (name);
  if (payload.empty ())
    return res;

  if (CanStoreRaw (payload))
    res->addChild (new gloox::Tag ("raw", payload));
  else
    res->addChild (EncodeXmlBase64 (payload).release ());

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

/* ************************************************************************** */

} // namespace charon
