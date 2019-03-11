#pragma once

#include <chrono>

namespace imqs {
namespace time {

enum class Weekday {
	Sunday = 0,
	Monday,
	Tuesday,
	Wednesday,
	Thursday,
	Friday,
	Saturday,
};

enum class Month {
	January = 1,
	February,
	March,
	April,
	May,
	June,
	July,
	August,
	September,
	October,
	November,
	December,
};

// Nanosecond-precision duration
// To form a duration, multiply an integer by one of the predefined constants Nanosecond, Second, etc.
//   Duration d = 30 * Second;
class IMQS_PAL_API Duration {
public:
	Duration() {}
	explicit Duration(int64_t nsec) : D(nsec) {}
	Duration operator*(int64_t m) const { return Duration(D * m); }
	Duration operator+(Duration b) const { return Duration(D + b.D); }
	Duration operator-(Duration b) const { return Duration(D - b.D); }
	bool     operator==(Duration b) const { return D == b.D; }
	bool     operator!=(Duration b) const { return D != b.D; }
	bool     operator<(Duration b) const { return D < b.D; }
	bool     operator>(Duration b) const { return D > b.D; }
	bool     operator<=(Duration b) const { return D <= b.D; }
	bool     operator>=(Duration b) const { return D >= b.D; }
	int64_t  Nanoseconds() const { return D; }
	double   Microseconds() const { return (double) D / 1000.0; }
	double   Milliseconds() const { return (double) D / 1000000.0; }
	double   Seconds() const { return (double) D / 1000000000.0; }
	double   Minutes() const { return (double) D / (1000000000.0 * 60); }
	double   Hours() const { return (double) D / (1000000000.0 * 3600); }

	std::chrono::nanoseconds Chrono() const { return std::chrono::nanoseconds(D); }
	operator std::chrono::nanoseconds() const { return std::chrono::nanoseconds(D); }

private:
	int64_t D = 0;
};

const Duration Nanosecond(1);
const Duration Microsecond(1000);
const Duration Millisecond(1000000);
const Duration Second(1000000000);
const Duration Minute(60 * (int64_t) 1000000000);
const Duration Hour(3600 * (int64_t) 1000000000);
const Duration Infinite((int64_t) 9223372036854775807ll); // Used as a special value to indicate concepts such as "wait forever"

inline Duration operator*(int64_t m, Duration d) { return d * m; }
// Date.
// This is a copy of the Go Time format, except that we omit the Location here.
// Everything that applies there, applies here too.
// Read the Go docs to understand what's going on here (https://golang.org/src/time/time.go).
// Be sure to read the part "Computations on time".
//
// For large date values and converting to/from significant other epochs, you may find the
// functions that deal with Microseconds useful. Duration can represent at most 292 years,
// so for dealing with adjustments larger than that, you need to use microsecond precision
// for the adjustments.
class IMQS_PAL_API Time {
public:
	Time() {}                                            // Construct null date
	static Time FromUnix(int64_t sec, int64_t nsec);     // Construct Time from Unix epoch. nsec may be greater than 1 billion.
	static Time FromUnix(double sec);                    // Construct Time from Unix epoch, fractional seconds.
	static Time FromInternal(int64_t sec, int32_t nsec); // Construct Time from internal representation
	static Time FromEpoch1601(int64_t t);                // Construct Time from 100-nanoseconds elapsed since January 1st, 1601 (Microsoft FILETIME)
	static Time FromHttp(const char* str, size_t len);   // Parse HTTP time (Sun, 06 Nov 1994 08:49:37 GMT)
	Time(int year, time::Month month, int day, int hour, int minute, int second, int nsec);

	static Time Now();

	int64_t       Unix() const;                                // Return seconds since Unix epoch
	int64_t       UnixNano() const;                            // Return nanoseconds since Unix epoch
	void          Internal(int64_t& sec, int32_t& nsec) const; // Returns internal representation
	int64_t       Epoch1601() const;                           // Returns number of 100-nanoseconds elapsed since January 1st, 1601 (Microsoft FILETIME)
	time::Weekday Weekday() const;
	int           Year() const;  // Compute year only (consider using DateComponents if retrieving more than just the year)
	time::Month   Month() const; // Compute month only (consider using DateComponents if retrieving more than just the month)
	int           Day() const;   // Compute day of month (starting at 1) (consider using DateComponents if retrieving more than just the month)

	void DateComponents(int& year, time::Month& month, int& day, int& yday) const;
	void TimeComponents(int& hour, int& min, int& sec) const;

	// Return nanoseconds beyond the second
	int Nanoseconds() const { return (int) Nsec; }

	// Return microseconds beyond the second
	int Microseconds() const { return (int) (Nsec / 1000); }

	// Return milliseconds beyond the second
	int Milliseconds() const { return (int) (Nsec / 1000000); }

	// We should adopt the Go date format string technique, which is genius. For now, we hardcode this.
	// Returns the number of characters written, excluding the null terminator
	// (length of 27 for Zulu, and 31 otherwise). In other words, buffer size must
	// be 28 for Zulu, and 32 otherwise.
	size_t      Format8601(char* buf, int timezone_offset_minutes = 0) const;
	std::string Format8601(int timezone_offset_minutes = 0) const;
	Error       Parse8601(const char* str, size_t len); // Parse ISO 8601 time (2013-10-11T22:14:15.003123Z)
	Error       Parse8601(const std::string& str);      // Parse ISO 8601 time (2013-10-11T22:14:15.003123Z)
	void        FormatHttp(char* buf) const;            // Buf must be at least 30 characters (29 + null terminator).
	std::string FormatHttp() const;
	Error       ParseHttp(const char* str, size_t len); // Parse HTTP time (Sun, 06 Nov 1994 08:49:37 GMT)

	int64_t SubMicro(Time t) const;         // Return number of microseconds between this and t.
	Time    PlusMicro(int64_t micro) const; // Return this time with microseconds added to it.
	int     Compare(Time t) const;          // Returns -1 when this BEFORE t; +1 when this AFTER t; 0 when this == t

	bool        IsNull() const { return Sec == 0 && Nsec == 0; }
	static bool IsLeapYear(uint32_t year) { return (year % 4 == 0) && !((year % 100 == 0) && (year % 400 != 0)); }
	Time        operator+(Duration d) const { return PlusNano(d.Nanoseconds()); }
	Time        operator-(Duration d) const { return PlusNano(-d.Nanoseconds()); }
	Time&       operator+=(Duration d) {
        AddNano(d.Nanoseconds());
        return *this;
	}
	Time& operator-=(Duration d) {
		AddNano(-d.Nanoseconds());
		return *this;
	}
	bool     operator==(Time d) const { return Sec == d.Sec && Nsec == d.Nsec; }
	bool     operator!=(Time d) const { return !(*this == d); }
	Duration operator-(Time d) const { return Sub(d); }
	bool     operator<(Time t) const;
	bool     operator<=(Time t) const;
	bool     operator>(Time t) const;
	bool     operator>=(Time t) const;

private:
	int64_t Sec  = 0;
	int32_t Nsec = 0;

	static const int64_t SecondsPerMinute = 60;
	static const int64_t SecondsPerHour   = 60 * 60;
	static const int64_t SecondsPerDay    = 24 * SecondsPerHour;
	static const int64_t SecondsPerWeek   = 7 * SecondsPerDay;
	static const int64_t DaysPer400Years  = 365 * 400 + 97;
	static const int64_t DaysPer100Years  = 365 * 100 + 24;
	static const int64_t DaysPer4Years    = 365 * 4 + 1;

	// The year of the zero Time.
	static const int64_t InternalYear = 1;

	// The unsigned zero year for internal calculations.
	// Must be 1 mod 400, and times before it will not compute correctly,
	// but otherwise can be changed at will.
	static const int64_t AbsoluteZeroYear = -292277022399;

	// Offsets to convert between internal and absolute or Unix times.
	// static const int64_t AbsoluteToInternal = (int64_t) ((AbsoluteZeroYear - InternalYear) * 365.2425 * SecondsPerDay); -- floating point arithmetic overflow
	static const int64_t AbsoluteToInternal = -9223371966579724800ll;
	static const int64_t InternalToAbsolute = -AbsoluteToInternal;

	static const int64_t UnixToInternal = (1969 * 365 + 1969 / 4 - 1969 / 100 + 1969 / 400) * SecondsPerDay;
	static const int64_t InternalToUnix = -UnixToInternal;

	static const int64_t Epoch1601ToInternal = (1600 * 365 + 1600 / 4 - 1600 / 100 + 1600 / 400) * SecondsPerDay;
	static const int64_t InternalToEpoch1601 = -Epoch1601ToInternal;

	uint64_t      Abs() const;
	Time          PlusNano(int64_t nsec) const;
	void          AddNano(int64_t nsec);
	Duration      Sub(Time d) const;
	time::Weekday AbsWeekday(uint64_t abs) const;
};

// Convenience function
IMQS_PAL_API Time Now();

// Read a performance counter timer, which is not tied to any particular epoch, but is
// intended to be accurate, and useful for profiling code.
IMQS_PAL_API int64_t PerformanceCounter();

// Return the frequency (ticks per second) of PerformanceCounter()
IMQS_PAL_API int64_t PerformanceFrequency();

inline std::string Time::Format8601(int timezone_offset_minutes) const {
	char buf[32];
	Format8601(buf, timezone_offset_minutes);
	return buf;
}

inline std::string Time::FormatHttp() const {
	char buf[30];
	FormatHttp(buf);
	return buf;
}

} // namespace time
} // namespace imqs