/*
    Charon - a transport system for GSP data
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

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

#ifndef CHARON_UTILS_METHODS_HPP
#define CHARON_UTILS_METHODS_HPP

#include <set>
#include <string>

namespace charon
{

/**
 * Returns the set of methods selected by the corresponding command-line
 * arguments.
 */
std::set<std::string> GetSelectedMethods ();

} // namespace charon

#endif // CHARON_UTILS_METHODS_HPP
