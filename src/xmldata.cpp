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

#include <zlib.h>

#include <glog/logging.h>

#include <sstream>
#include <vector>

namespace charon
{

namespace
{

/** Minimum length of data to even attempt compression.  */
constexpr size_t MIN_COMPRESS_LEN = 128;

/**
 * Minimum compression factor before we actually send compressed data.
 * Note that we get an extra overhead from base64, so this needs to be small
 * enough to make it worthwhile.
 *
 * The value here is the maximum percentage that the compressed size can
 * be in terms of raw payload size.
 */
constexpr size_t MAX_COMPRESSED_PERCENT = 70;

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
 * Compresses the given data with zlib's utility compress.
 */
std::string
Compress (const std::string& data)
{
  const auto* in = reinterpret_cast<const Bytef*> (data.data ());
  uLongf destLen = compressBound (data.size ());

  std::string res;
  res.resize (destLen);
  auto* out = reinterpret_cast<Bytef*> (&res[0]);

  CHECK_EQ (compress (out, &destLen, in, data.size ()), Z_OK);
  res.resize (destLen);

  VLOG (2)
      << "Compressed " << data.size ()
      << " input bytes into " << res.size ();

  return res;
}

/**
 * Tries to uncompress data with zlib's utility uncompress.  Returns
 * false if there is some error.  The expected size of the uncompressed
 * data must be passed in.
 */
bool
Uncompress (const std::string& compressed, const size_t len, std::string& data)
{
  data.resize (len);

  const auto* in = reinterpret_cast<const Bytef*> (compressed.data ());
  auto* out = reinterpret_cast<Bytef*> (&data[0]);
  uLongf destLen = len;

  const int rc = uncompress (out, &destLen, in, compressed.size ());
  if (rc != Z_OK)
    {
      LOG (WARNING) << "zlib uncompress failed with code " << rc;
      return false;
    }

  if (destLen != len)
    {
      LOG (WARNING)
          << "Uncompressed data has wrong size " << destLen
          << " (expected " << len << ")";
      return false;
    }

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

  if (tag.name () == "zlib")
    {
      std::istringstream sizeStr(tag.findAttribute ("size"));
      size_t len;
      sizeStr >> len;

      std::string compressed;
      if (!DecodeXmlPayload (tag, compressed))
        {
          LOG (WARNING) << "Failed to extract <zlib> compressed data";
          return false;
        }

      return Uncompress (compressed, len, payload);
    }

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

  /* Heuristically estimate if it makes sense to compress the data.  We do
     that only for data that has a somewhat meaningful length, and also only
     if the compressed size is sufficiently small to make it worthwhile even
     with adding base64 on top.  */
  if (payload.size () >= MIN_COMPRESS_LEN)
    {
      const std::string compressed = Compress (payload);
      if (compressed.size () * 100 <= MAX_COMPRESSED_PERCENT * payload.size ())
        {
          VLOG (2) << "Sending compressed payload";

          std::ostringstream size;
          size << payload.size ();

          auto zlibTag = std::make_unique<gloox::Tag> ("zlib");
          zlibTag->addAttribute ("size", size.str ());
          zlibTag->addChild (EncodeXmlBase64 (compressed).release ());
          res->addChild (zlibTag.release ());

          return res;
        }

      VLOG (2) << "Compression ratio is not sufficient, sending uncompressed";
    }
  else
    VLOG (2)
        << "Not attempting compression of payload with size "
        << payload.size ();

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
  size_t len = 0;
  for (const auto* child : tag.children ())
    {
      std::string cur;
      if (!DecodePayloadTag (*child, cur))
        return false;

      if (len + cur.size () > MAX_XML_PAYLOAD_SIZE)
        {
          LOG (WARNING)
              << "Exceeded maximum payload size of " << MAX_XML_PAYLOAD_SIZE
              << ", ignoring payload";
          return false;
        }

      res << cur;
      len += cur.size ();
    }

  payload = res.str ();
  CHECK_EQ (payload.size (), len);
  CHECK_LE (payload.size (), MAX_XML_PAYLOAD_SIZE);

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
