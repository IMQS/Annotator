#include "pch.h"
#include "Event.h"

namespace imqs {
namespace sync {

Event::Event(bool autoReset) : AutoReset(autoReset) {
}

Event::~Event() {
}

void Event::Reset() {
	std::lock_guard<std::mutex> lock(M);
	Signalled = false;
}

void Event::Signal() {
	{
		std::lock_guard<std::mutex> lock(M);
		Signalled = true;
	}
	// If this is an AutoReset event, then only wake one thread, because by definition, only
	// one thread can be woken in this mode.
	// This is a performance optimization. I don't know how notify_all() differs from notify_one()
	// internally, but I think it's good to assume that notify_one() is cheaper.
	if (AutoReset)
		CV.notify_one();
	else
		CV.notify_all();
}

void Event::Wait() {
	Wait(time::Infinite);
}

bool Event::Wait(time::Duration duration) {
	if (duration.Nanoseconds() == 0)
		return WaitNoHang();

	bool                         res = true;
	std::unique_lock<std::mutex> lock(M);
	if (duration == time::Infinite)
		CV.wait(lock, [this] { return Signalled; });
	else
		res = CV.wait_for(lock, duration.Chrono(), [this] { return Signalled; });
	if (res && AutoReset)
		Signalled = false;
	return res;
}

bool Event::WaitNoHang() {
	std::unique_lock<std::mutex> lock(M);
	bool                         res = Signalled;
	if (res && AutoReset)
		Signalled = false;
	return res;
}

} // namespace sync
} // namespace imqs
