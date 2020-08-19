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
  const char* tests[] =
    {
      "",
      "foobar",
      "abc\ndef<>&",
    };

  for (const std::string t : tests)
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
  tag.addChild (new gloox::Tag ("raw", " "));
  tag.addChild (new gloox::Tag ("raw", "bar"));

  std::string val;
  ASSERT_TRUE (DecodeXmlPayload (tag, val));
  EXPECT_EQ (val, "foo bar");
}

TEST_F (XmlPayloadTests, InvalidTag)
{
  gloox::Tag tag("foo");
  tag.addChild (new gloox::Tag ("invalid", "data"));

  std::string val;
  EXPECT_FALSE (DecodeXmlPayload (tag, val));
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
