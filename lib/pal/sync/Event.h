#pragma once

#include "../Time_.h"

namespace imqs {
namespace sync {

/* Event is an attempt to achieve similar functionality to that provided
by the Windows Event concept.

If AutoReset is true, then the first thread to wake from a Wait() will
set the signal back to false.

If AutoReset is false, then the event remains signaled until you call Reset().

WARNING: Do not try to use this as a semaphore. Even for an AutoReset event,
every call to Signal is not guaranteed to correspond to one call to Wait().
If two threads call Signal, then they might both be setting Signal to true,
before any waiting threads are woken up.
*/
class IMQS_PAL_API Event {
public:
	Event(bool autoReset);
	~Event();

	void Reset();                       // Reset the signal to false
	void Signal();                      // Signal the event
	void Wait();                        // Wait for infinity
	bool Wait(time::Duration duration); // Returns true if the wait returned because the event was signaled. Returns false if the timeout elapsed.
	bool WaitNoHang();                  // Alias for Wait(0). Returns true if the event was signaled.

private:
	std::condition_variable CV;
	std::mutex              M;
	bool                    AutoReset = false;
	bool                    Signalled = false;
};
} // namespace sync
} // namespace imqs
