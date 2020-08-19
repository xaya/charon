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

#ifndef CHARON_XMLDATA_HPP
#define CHARON_XMLDATA_HPP

#include <gloox/tag.h>

#include <json/json.h>

#include <memory>
#include <string>

namespace charon
{

/**
 * Encodes a payload string into a gloox tag of the given name.
 */
std::unique_ptr<gloox::Tag> EncodeXmlPayload (const std::string& name,
                                              const std::string& payload);

/**
 * Decodes the payload from a given tag.  Returns true on success, and false
 * if no valid payload was found.
 */
bool DecodeXmlPayload (const gloox::Tag& tag, std::string& payload);

/**
 * Encodes a JSON value as payload.
 */
std::unique_ptr<gloox::Tag> EncodeXmlJson (const std::string& name,
                                           const Json::Value& val);

/**
 * Decodes a payload as JSON from a given tag.  Returns true on success and
 * false if no payload was found or it failed to parse as JSON.
 */
bool DecodeXmlJson (const gloox::Tag& tag, Json::Value& val);

} // namespace charon

#endif // CHARON_XMLDATA_HPP
