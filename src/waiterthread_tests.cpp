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

#include "testutils.hpp"

#include <gtest/gtest.h>

#include <glog/logging.h>

namespace charon
{
namespace
{

/* ************************************************************************** */

/**
 * An instance of an UpdatableState, its WaiterThread and an associated
 * update handler that allows expecting update calls.
 */
class TestWaiter
{

private:

  /** The underlying UpdatableState.  */
  UpdatableState::Handle upd;

  /** The WaiterThread instance from our state.  */
  std::unique_ptr<WaiterThread> thread;

  /** Mutex for the callback state and cv.  */
  std::mutex mut;

  /** State with which the update callback was last invoked.  */
  Json::Value lastCbState;

  /** Condition variable for update callbacks.  */
  std::condition_variable cv;

public:

  explicit TestWaiter (const std::string& nm)
  {
    upd = UpdatableState::Create ();

    thread = upd->NewWaiter (nm);
    thread->SetUpdateHandler ([this] (const Json::Value& s)
      {
        VLOG (1) << "Update callback: " << s;

        std::lock_guard<std::mutex> lock(mut);

        EXPECT_TRUE (lastCbState.isNull ()) << "Extra update callback";
        lastCbState = s;

        cv.notify_all ();
      });

    thread->Start ();
    std::this_thread::sleep_for (std::chrono::milliseconds (10));
  }

  ~TestWaiter ()
  {
    thread->Stop ();
  }

  /**
   * Returns the current state.
   */
  Json::Value
  GetCurrentState () const
  {
    return thread->GetCurrentState ();
  }

  /**
   * Expects an update callback for the given new state.
   */
  void
  ExpectUpdate (const std::string& id, const std::string& value)
  {
    LOG (INFO) << "Expecting update to: " << id << ", " << value;
    std::unique_lock<std::mutex> lock(mut);
    while (true)
      {
        if (!lastCbState.isNull ())
          {
            EXPECT_EQ (lastCbState, GetStateJson (id, value));
            lastCbState = Json::Value ();
            return;
          }

        cv.wait (lock);
      }
  }

  Json::Value
  GetStateJson (const std::string& id, const std::string& value)
  {
    return upd->GetStateJson (id, value);
  }

  void
  SetState (const std::string& id, const std::string& value)
  {
    return upd->SetState (id, value);
  }

  void
  SetShouldFail (const bool val)
  {
    upd->SetShouldFail (val);
  }

  unsigned
  GetNumCalls () const
  {
    return upd->GetNumCalls ();
  }

  /**
   * Sets the backoff value of the waiter thread.
   */
  void
  SetBackoff (const std::chrono::milliseconds& val)
  {
    thread->SetBackoff (val);
  }

};

/* ************************************************************************** */

using WaiterThreadTests = testing::Test;

TEST_F (WaiterThreadTests, Update)
{
  TestWaiter w("test");
  w.SetState ("first", "foo");
  w.ExpectUpdate ("first", "foo");
  EXPECT_EQ (w.GetCurrentState (), ParseJson (R"({
    "id": "first",
    "value": "foo"
  })"));

  w.SetState ("second", "bar");
  w.ExpectUpdate ("second", "bar");

  /* The same ID should not trigger another upate.  */
  w.SetState ("second", "baz");

  w.SetState ("third", "final");
  w.ExpectUpdate ("third", "final");
}

TEST_F (WaiterThreadTests, TwoWaiters)
{
  TestWaiter w1("test 1");
  TestWaiter w2("test 2");

  w1.SetState ("first", "1");
  w2.SetState ("first", "2");
  w1.ExpectUpdate ("first", "1");
  w2.ExpectUpdate ("first", "2");

  w1.SetState ("second", "1");
  w2.SetState ("second", "2");
  w1.ExpectUpdate ("second", "1");
  w2.ExpectUpdate ("second", "2");
}

TEST_F (WaiterThreadTests, ErrorBackoff)
{
  TestWaiter w("test");
  w.SetBackoff (std::chrono::milliseconds (100));

  /* If the waiter call fails, we should not go into a loop spamming
     more calls as fast as possible.  */
  w.SetShouldFail (true);
  std::this_thread::sleep_for (std::chrono::milliseconds (10));
  EXPECT_LT (w.GetNumCalls (), 5);

  /* Once the error stops, it should get back to being normal (and especially
     update faster than the backoff time if there are real updates).  */
  w.SetShouldFail (false);
  std::this_thread::sleep_for (std::chrono::milliseconds (200));
  using Clock = std::chrono::steady_clock;
  const auto before = Clock::now ();
  w.SetState ("update", "foo");
  w.ExpectUpdate ("update", "foo");
  const auto after = Clock::now ();
  EXPECT_LT (after - before, std::chrono::milliseconds (10));
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace charon
