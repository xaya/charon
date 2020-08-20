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

#include "testutils.hpp"

#include <gtest/gtest.h>

#include <glog/logging.h>

namespace charon
{
namespace
{

/* ************************************************************************** */

using XmlPayloadTests = testing::Test;

TEST_F (XmlPayloadTests, EncodedTagName)
{
  const auto tag = EncodeXmlPayload ("mytag", "foo");
  EXPECT_EQ (tag->name (), "mytag");
}

TEST_F (XmlPayloadTests, Roundtrip)
{
  const std::string tests[] =
    {
      "",
      std::string ("\0", 1),
      "foobar",
      "abc\ndef<>&",
      u8"äöü",
      std::string ("\0abc\xFFzyx\0", 9),
      std::string (MAX_XML_PAYLOAD_SIZE, 'x'),
    };

  for (const auto& t : tests)
    {
      VLOG (1) << "Testing with:\n" << t;

      const auto tag = EncodeXmlPayload ("foo", t);

      std::string recovered;
      ASSERT_TRUE (DecodeXmlPayload (*tag, recovered));

      EXPECT_EQ (recovered, t);
    }
}

TEST_F (XmlPayloadTests, SplitData)
{
  gloox::Tag tag("foo");
  tag.addChild (new gloox::Tag ("raw", "foo"));
  tag.addChild (new gloox::Tag ("base64", "IA=="));
  tag.addChild (new gloox::Tag ("raw", "bar"));

  std::string val;
  ASSERT_TRUE (DecodeXmlPayload (tag, val));
  EXPECT_EQ (val, "foo bar");
}

TEST_F (XmlPayloadTests, Base64Example)
{
  gloox::Tag tag("foo");
  tag.addChild (new gloox::Tag ("base64",
                                "VGhpcyBpcyBhbiBleGFtcGxlIHN0cmluZy4="));

  std::string val;
  ASSERT_TRUE (DecodeXmlPayload (tag, val));
  EXPECT_EQ (val, "This is an example string.");
}

TEST_F (XmlPayloadTests, InvalidTag)
{
  gloox::Tag tag("foo");
  tag.addChild (new gloox::Tag ("invalid", "data"));

  std::string val;
  EXPECT_FALSE (DecodeXmlPayload (tag, val));
}

TEST_F (XmlPayloadTests, MaxSize)
{
  const std::string halfStr(MAX_XML_PAYLOAD_SIZE / 2, 'x');

  gloox::Tag tag("foo");
  tag.addChild (new gloox::Tag ("raw", halfStr));
  tag.addChild (new gloox::Tag ("raw", halfStr));
  tag.addChild (new gloox::Tag ("raw", ""));

  std::string decoded;
  ASSERT_TRUE (DecodeXmlPayload (tag, decoded));
  EXPECT_EQ (decoded, halfStr + halfStr);

  tag.addChild (new gloox::Tag ("raw", "x"));
  EXPECT_FALSE (DecodeXmlPayload (tag, decoded));
}

/* ************************************************************************** */

class XmlBase64Tests : public testing::Test
{

protected:

  /**
   * Runs a roundtrip with base64 encoding.
   */
  static void
  CheckRoundtrip (const std::string& input)
  {
    gloox::Tag tag("foo");
    tag.addChild (EncodeXmlBase64 (input).release ());

    std::string recovered;
    ASSERT_TRUE (DecodeXmlPayload (tag, recovered));
    EXPECT_EQ (recovered, input);
  }

  /**
   * Decodes the given base64 and expects the resulting output.
   */
  static void
  CheckDecode (const std::string& encoded, const std::string& expected)
  {
    gloox::Tag tag("foo");
    tag.addChild (new gloox::Tag ("base64", encoded));

    std::string decoded;
    ASSERT_TRUE (DecodeXmlPayload (tag, decoded)) << encoded;
    EXPECT_EQ (decoded, expected);
  }

};

TEST_F (XmlBase64Tests, BasicRoundtrips)
{
  CheckRoundtrip ("");
  CheckRoundtrip ("a");
  CheckRoundtrip ("ab");
  CheckRoundtrip ("abc");
  CheckRoundtrip ("abcd");
  CheckRoundtrip ("\0");
  CheckRoundtrip ("abc\xFFxyz\0");
}

TEST_F (XmlBase64Tests, Lengths)
{
  for (unsigned n = 0; n < 1024; ++n)
    {
      CheckRoundtrip (std::string (n, 'x'));
      CheckRoundtrip (std::string (n, '\0'));
      CheckRoundtrip (std::string (n, '\xFF'));
    }
}

TEST_F (XmlBase64Tests, Valid)
{
  CheckDecode ("", "");
  CheckDecode ("YWJj", "abc");
  CheckDecode ("YWI=", "ab");
  CheckDecode ("YWI=\n", "ab");
}

TEST_F (XmlBase64Tests, Invalid)
{
  const char* tests[] =
    {
      "AAA",
      " AAA",
      "AA.A",
      "=",
      "==",
      "===",
      "====",
      "AAA=====",
      "AAAA====",
      "AAA=A===",
      "AA===",
      " AA=== ",
      "invalid base64",
    };

  for (const std::string t : tests)
    {
      VLOG (1) << "Testing with: " << t;

      gloox::Tag tag("foo");
      tag.addChild (new gloox::Tag ("base64", t));

      std::string dummy;
      EXPECT_FALSE (DecodeXmlPayload (tag, dummy)) << dummy;
    }
}

/* ************************************************************************** */

using XmlJsonTests = testing::Test;

TEST_F (XmlJsonTests, EncodedTagName)
{
  const auto tag = EncodeXmlJson ("mytag", ParseJson ("{}"));
  EXPECT_EQ (tag->name (), "mytag");
}

TEST_F (XmlJsonTests, Roundtrip)
{
  const char* tests[] =
    {
      "42",
      "false",
      "null",
      "-1.5",
      R"("this is a JSON string\nwith newlines")",
      "[1, 2, 3]",
      R"(
        {
          "some": "field",
          "int": 100,
          "obj": {},
          "arr": [1, {}, false]
        }
      )"
    };

  for (const std::string t : tests)
    {
      VLOG (1) << "Testing with:\n" << t;
      const auto value = ParseJson (t);

      const auto tag = EncodeXmlJson ("foo", value);

      Json::Value recovered;
      ASSERT_TRUE (DecodeXmlJson (*tag, recovered));

      EXPECT_EQ (recovered, value);
    }
}

TEST_F (XmlJsonTests, SplitPayload)
{
  gloox::Tag tag("foo");
  tag.addChild (new gloox::Tag ("raw", "[1,"));
  tag.addChild (new gloox::Tag ("raw", "2"));
  tag.addChild (new gloox::Tag ("raw", ", 3]"));

  Json::Value val;
  ASSERT_TRUE (DecodeXmlJson (tag, val));
  EXPECT_EQ (val, ParseJson ("[1, 2, 3]"));
}

TEST_F (XmlJsonTests, InvalidJson)
{
  const char* tests[] =
    {
      "",
      "invalid JSON",
      "{} junk",
    };

  for (const std::string t : tests)
    {
      VLOG (1) << "Testing with:\n" << t;

      Json::Value dummy;
      EXPECT_FALSE (DecodeXmlJson (*EncodeXmlPayload ("foo", t), dummy));
    }
}

TEST_F (XmlJsonTests, InvalidPayload)
{
  gloox::Tag tag("foo");
  tag.addChild (new gloox::Tag ("invalid", "data"));

  Json::Value dummy;
  EXPECT_FALSE (DecodeXmlJson (tag, dummy));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace charon
