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

namespace
{

/** The default backoff time used for waiter calls.  */
const auto DEFAULT_BACKOFF = std::chrono::seconds (5);

} // anonymous namespace

WaiterThread::WaiterThread (std::unique_ptr<NotificationType> t,
                            std::unique_ptr<UpdateWaiter> w)
  : type(std::move (t)), waiter(std::move (w)),
    backoff(DEFAULT_BACKOFF)
{}

WaiterThread::~WaiterThread ()
{
  CHECK (loop == nullptr);
}

void
WaiterThread::RunLoop ()
{
  using Clock = std::chrono::steady_clock;
  while (!shouldStop)
    {
      Json::Value result;
      const auto before = Clock::now ();
      if (!waiter->WaitForUpdate (result))
        {
          /* Make sure to wait for the backoff time to be elapsed in case
             the call failed.  We take the time of the call itself into account,
             though, to e.g. handle timeout errors better.  */
          const auto after = Clock::now ();
          const auto toSleep = backoff - (after - before);
          if (toSleep > decltype (toSleep)::zero ())
            {
              const auto sleepMs
                  = std::chrono::duration_cast<std::chrono::milliseconds> (
                        toSleep);
              LOG (INFO)
                  << "Waiter call failed, backing off for "
                  << sleepMs.count () << " ms";
              std::this_thread::sleep_for (toSleep);
            }
          continue;
        }

      if (result.isNull ())
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
