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

/* This header includes declarations that are used only internally, i.e.
   from xmldata.cpp and also for testing (but should not be used directly
   by anyone using Charon as a library).  */

#ifndef CHARON_XMLDATA_INTERNAL_HPP
#define CHARON_XMLDATA_INTERNAL_HPP

#include "xmldata.hpp"

namespace charon
{

/**
 * Encodes a payload string as base64 tag and returns the <base64> tag.
 */
std::unique_ptr<gloox::Tag> EncodeXmlBase64 (const std::string& payload);

} // namespace charon

#endif // CHARON_XMLDATA_INTERNAL_HPP
