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

#include "waiterthread.hpp"

#include <glog/logging.h>

namespace charon
{

WaiterThread::~WaiterThread ()
{
  CHECK (loop == nullptr);
}

void
WaiterThread::RunLoop ()
{
  while (!shouldStop)
    {
      Json::Value result;
      if (!waiter->WaitForUpdate (result))
        continue;

      std::lock_guard<std::mutex> lock(mut);
      const Json::Value newId = type->ExtractStateId (result);

      if (!currentState.isNull ()
            && type->ExtractStateId (currentState) == newId)
        continue;

      VLOG (1)
          << "Found new best state ID for " << type->GetType ()
          << ": " << newId;
      currentState = result;

      if (cb)
        cb (currentState);
    }
}

void
WaiterThread::Start ()
{
  CHECK (loop == nullptr);
  LOG (INFO) << "Starting waiter thread for " << type->GetType () << "...";

  currentState = Json::Value ();
  shouldStop = false;
  loop = std::make_unique<std::thread> ([this] ()
    {
      RunLoop ();
    });
}

void
WaiterThread::Stop ()
{
  if (loop == nullptr)
    return;

  LOG (INFO) << "Stopping waiter thread for " << type->GetType () << "...";

  shouldStop = true;
  loop->join ();
  loop.reset ();
}

Json::Value
WaiterThread::GetCurrentState () const
{
  CHECK (loop != nullptr);
  std::lock_guard<std::mutex> lock(mut);
  return currentState;
}

void
WaiterThread::ClearUpdateHandler ()
{
  std::lock_guard<std::mutex> lock(mut);
  cb = UpdateHandler ();
  CHECK (!cb);
}

void
WaiterThread::SetUpdateHandler (const UpdateHandler& h)
{
  std::lock_guard<std::mutex> lock(mut);
  cb = h;
}

} // namespace charon
