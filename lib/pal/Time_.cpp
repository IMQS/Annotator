#include "pch.h"
#include "Time_.h"

namespace imqs {
namespace time {

#ifdef _WIN32
volatile uint32_t MyGetSystemTimePreciseAsFileTime_Initialized;
VOID(WINAPI* MyGetSystemTimePreciseAsFileTime)
(_Out_ LPFILETIME lpSystemTimeAsFileTime);
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////

const int64_t Time::SecondsPerMinute;
const int64_t Time::SecondsPerHour;
const int64_t Time::SecondsPerDay;
const int64_t Time::SecondsPerWeek;
const int64_t Time::DaysPer400Years;
const int64_t Time::DaysPer100Years;
const int64_t Time::DaysPer4Years;
const int64_t Time::InternalYear;
const int64_t Time::UnixToInternal;
const int64_t Time::InternalToUnix;

// daysBefore[m] counts the number of days in a non-leap year
// before month m begins.  There is an entry for m=12, counting
// the number of days before January of next year (365).
static const int DaysBefore[] = {
    0,
    31,
    31 + 28,
    31 + 28 + 31,
    31 + 28 + 31 + 30,
    31 + 28 + 31 + 30 + 31,
    31 + 28 + 31 + 30 + 31 + 30,
    31 + 28 + 31 + 30 + 31 + 30 + 31,
    31 + 28 + 31 + 30 + 31 + 30 + 31 + 31,
    31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30,
    31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31,
    31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30,
    31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30 + 31,
};

// Normalize alters nhi, nlo such that
//	hi * base + lo == nhi * base + nlo
//	0 <= nlo < base
template <typename T>
void Normalize(T& hi, T& lo, T base) {
	if (lo < 0) {
		auto n = (-lo - 1) / base + 1;
		hi -= n;
		lo += n * base;
	}
	if (lo >= base) {
		auto n = lo / base;
		hi += n;
		lo -= n * base;
	}
}

static const char* WeekDayTable[] = {
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday",
};

static const char* MonthTable[] = {
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December",
};

template <typename TCH, int len>
int AtoILen(const TCH* str) {
	// 123
	int v = 0;
	for (int i = 0; i < len; i++) {
		v *= 10;
		v += str[i] - '0';
	}
	return v;
}

// Returns the month, or January upon failure
template <typename TCH>
Month TMonthFromName(int n, const TCH* name) {
	/*

	m = {'Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'}
	all = {}
	for k,v in pairs(m) do
		v = v:upper()
		x = v:byte(1) * 65536 + v:byte(2) * 256 + v:byte(3)
		all[#all + 1] = string.format('case 0x%X: return %d;', x, k - 1)
	end

	table.sort(all)
	for k,v in pairs(all) do print( v ) end

	*/
	if (n < 3)
		return Month::January;
	uint32_t v = (::toupper(name[0]) << 16) | (::toupper(name[1]) << 8) | ::toupper(name[2]);

	switch (v) {
	case 0x415052: return Month::April;
	case 0x415547: return Month::August;
	case 0x444543: return Month::December;
	case 0x464542: return Month::February;
	case 0x4A414E: return Month::January;
	case 0x4A554C: return Month::July;
	case 0x4A554E: return Month::June;
	case 0x4D4152: return Month::March;
	case 0x4D4159: return Month::May;
	case 0x4E4F56: return Month::November;
	case 0x4F4354: return Month::October;
	case 0x534550: return Month::September;
	}

	return Month::January;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

Time::Time(int year, time::Month month, int day, int hour, int minute, int second, int nsec) {
	// Normalize month, overflowing into year.
	int m = (int) month - 1;
	Normalize(year, m, 12);
	month = (time::Month)(m + 1);

	// Normalize nsec, sec, min, hour, overflowing into day.
	Normalize(second, nsec, 1000000000);
	Normalize(minute, second, 60);
	Normalize(hour, minute, 60);
	Normalize(day, hour, 24);

	uint64_t y = uint64_t(int64_t(year) - AbsoluteZeroYear);

	// Compute days since the absolute epoch.

	// Add in days from 400-year cycles.
	auto n = y / 400;
	y -= 400 * n;
	auto d = DaysPer400Years * n;

	// Add in 100-year cycles.
	n = y / 100;
	y -= 100 * n;
	d += DaysPer100Years * n;

	// Add in 4-year cycles.
	n = y / 4;
	y -= 4 * n;
	d += DaysPer4Years * n;

	// Add in non-leap years.
	n = y;
	d += 365 * n;

	// Add in days before this month.
	d += uint64_t(DaysBefore[(int) month - 1]);
	if (IsLeapYear(year) && month >= time::Month::March)
		d++; // February 29

	// Add in days before today.
	d += uint64_t(day - 1);

	// Add in time elapsed today.
	auto abs = d * SecondsPerDay;
	abs += uint64_t(hour * SecondsPerHour + minute * SecondsPerMinute + second);

	auto unix = int64_t(abs) + (AbsoluteToInternal + InternalToUnix);

	Sec  = unix + UnixToInternal;
	Nsec = int32_t(nsec);
}

Time Time::Now() {
#ifdef _WIN32
	// Delay-load GetSystemTimePreciseAsFileTime so that we can run on a Windows 7 class OS
	if (!MyGetSystemTimePreciseAsFileTime_Initialized) {
		// This would cause a race condition if a pointer-sized assignment was not atomic. I'm not sure if this is an issue on ARM.
		auto kernel = GetModuleHandleA("kernel32.dll");
		IMQS_ASSERT(kernel);
		MyGetSystemTimePreciseAsFileTime             = (decltype(MyGetSystemTimePreciseAsFileTime)) GetProcAddress(kernel, "GetSystemTimePreciseAsFileTime");
		MyGetSystemTimePreciseAsFileTime_Initialized = 1;
	}
	FILETIME ft;
	if (MyGetSystemTimePreciseAsFileTime)
		MyGetSystemTimePreciseAsFileTime(&ft);
	else
		GetSystemTimeAsFileTime(&ft);

	return FromEpoch1601((int64_t)((uint64_t) ft.dwHighDateTime << 32 | (uint64_t) ft.dwLowDateTime));
#else
	timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	return FromUnix(t.tv_sec, t.tv_nsec);
#endif
}

// Returns a date corresponding to the given Unix time,
// sec seconds and nsec nanoseconds since January 1, 1970 UTC.
// It is valid to pass nsec outside the range [0, 999999999].
Time Time::FromUnix(int64_t sec, int64_t nsec) {
	const int64_t billion = 1000000000;
	if (nsec < 0 || nsec >= billion) {
		int64_t n = nsec / billion;
		sec += n;
		nsec -= n * billion;
		if (nsec < 0) {
			nsec += billion;
			sec--;
		}
	}
	return FromInternal(sec + UnixToInternal, (int32_t) nsec);
}

Time Time::FromUnix(double sec) {
	double ipart;
	double frac = modf(sec, &ipart);
	return Time::FromUnix((int64_t) ipart, (int64_t)(frac * 1000000000.0));
}

Time Time::FromEpoch1601(int64_t t) {
	int64_t tsec = t / (1000000000 / 100);
	int64_t trem = t % (1000000000 / 100);
	Time    d;
	d.Sec  = tsec + Epoch1601ToInternal;
	d.Nsec = (int32_t) trem * 100;
	return d;
}

Time Time::FromHttp(const char* str, size_t len) {
	Time t;
	t.ParseHttp(str, len);
	return t;
}

// Returns t as a Unix time, the number of seconds elapsed since January 1, 1970 UTC.
int64_t Time::Unix() const {
	return Sec + InternalToUnix;
}

// Returns t as a Unix time, the number of nanoseconds elapsed
// since January 1, 1970 UTC. The result is undefined if the Unix time
// in nanoseconds cannot be represented by an int64. Note that this
// means the result of calling UnixNano on the zero Time is undefined.
int64_t Time::UnixNano() const {
	return (Sec + InternalToUnix) * (int64_t) 1000000000 + int64_t(Nsec);
}

void Time::Internal(int64_t& sec, int32_t& nsec) const {
	sec  = Sec;
	nsec = Nsec;
}

int64_t Time::Epoch1601() const {
	return (Sec + InternalToEpoch1601) * (1000000000 / 100) + Nsec / 100;
}

uint64_t Time::Abs() const {
	return uint64_t(Sec + InternalToAbsolute);
}

void Time::DateComponents(int& year, time::Month& month, int& day, int& yday) const {
	const bool full = true;
	uint64_t   abs  = Abs();

	// Split into time and day.
	auto d = abs / SecondsPerDay;

	// Account for 400 year cycles.
	auto n = d / DaysPer400Years;
	auto y = 400 * n;
	d -= DaysPer400Years * n;

	// Cut off 100-year cycles.
	// The last cycle has one extra leap year, so on the last day
	// of that year, day / daysPer100Years will be 4 instead of 3.
	// Cut it back down to 3 by subtracting n>>2.
	n = d / DaysPer100Years;
	n -= n >> 2;
	y += 100 * n;
	d -= DaysPer100Years * n;

	// Cut off 4-year cycles.
	// The last cycle has a missing leap year, which does not
	// affect the computation.
	n = d / DaysPer4Years;
	y += 4 * n;
	d -= DaysPer4Years * n;

	// Cut off years within a 4-year cycle.
	// The last year is a leap year, so on the last day of that year,
	// day / 365 will be 4 instead of 3.  Cut it back down to 3
	// by subtracting n>>2.
	n = d / 365;
	n -= n >> 2;
	y += n;
	d -= 365 * n;

	year = int(int64_t(y) + AbsoluteZeroYear);
	yday = int(d);

	if (!full)
		return;

	day = yday;
	if (IsLeapYear(year)) {
		if (day > 31 + 29 - 1) {
			// After leap day; pretend it wasn't there.
			day--;
		} else if (day == 31 + 29 - 1) {
			// Leap day.
			month = time::Month::February;
			day   = 29;
			return;
		}
	}

	// Estimate month on assumption that every month has 31 days.
	// The estimate may be too low by at most one month, so adjust.
	month      = time::Month(day / 31);
	auto end   = int(DaysBefore[(int) month + 1]);
	int  begin = 0;
	if (day >= end) {
		month = (time::Month)((int) month + 1);
		begin = end;
	} else {
		begin = int(DaysBefore[(int) month]);
	}

	month = (time::Month)((int) month + 1); // because January is 1
	day   = day - begin + 1;
}

void Time::TimeComponents(int& hour, int& min, int& sec) const {
	uint64_t abs = Abs();

	sec  = int(abs % SecondsPerDay);
	hour = sec / SecondsPerHour;
	sec -= hour * SecondsPerHour;
	min = sec / SecondsPerMinute;
	sec -= min * SecondsPerMinute;
}

Components Time::Components() const {
	time::Components components;
	DateComponents(components.Year, components.Month, components.Day, components.YearDay);
	TimeComponents(components.Hour, components.Minute, components.Second);
	return components;
}

time::Weekday Time::Weekday() const {
	return AbsWeekday(Abs());
}

time::Weekday Time::AbsWeekday(uint64_t abs) const {
	// January 1 of the absolute year, like January 1 of 2001, was a Monday.
	uint64_t sec = (abs + uint64_t(Weekday::Monday) * SecondsPerDay) % SecondsPerWeek;
	return (time::Weekday)((int) sec / (int) SecondsPerDay);
}

int Time::Year() const {
	int         y, d, yd;
	time::Month m;
	DateComponents(y, m, d, yd);
	return y;
}

time::Month Time::Month() const {
	int         y, d, yd;
	time::Month m;
	DateComponents(y, m, d, yd);
	return m;
}

int Time::Day() const {
	int         y, d, yd;
	time::Month m;
	DateComponents(y, m, d, yd);
	return d;
}

size_t Time::Format8601(char* buf, int timezone_offset_minutes) const {
	// 2013-10-11T22:14:15.003123Z
	// 2013-10-11T22:14:15.003123+0200

	int tmoffset = -timezone_offset_minutes;

	auto copy = *this;
	copy -= tmoffset * Minute;

	int         year, day, yday;
	time::Month month;
	copy.DateComponents(year, month, day, yday);

	int hour, min, sec;
	copy.TimeComponents(hour, min, sec);

	unsigned bias_h = abs(tmoffset / 60);
	unsigned bias_m = abs(tmoffset % 60);

	if (tmoffset == 0) {
		sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02d.%06dZ", year, (int) month, day, hour, min, sec, Nsec / 1000);
		return 27;
	} else {
		sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02d.%06d%c%02u%02u", year, (int) month, day, hour, min, sec, Nsec / 1000, tmoffset <= 0 ? '+' : '-', bias_h, bias_m);
		return 31;
	}
}

static StaticError Err8601DateParse("Invalid ISO 8601 date format. Must be like \"1994-11-06T08:49:37\"");
static StaticError Err8601BadYear("ISO8601 Date: Bad year value. Must be valid 4 digit number");
static StaticError Err8601BadMonth("ISO8601 Date: Bad month value. Must be between 01-12");
static StaticError Err8601BadDay("ISO8601 Date: Bad day of month value. Must be between 01-32");
static StaticError Err8601BadHour("ISO8601 Time: Bad hour value. Must be between 00-23");
static StaticError Err8601BadMinute("ISO8601 Time: Bad minute value. Must be between 00-59");
static StaticError Err8601BadSecond("ISO8601 Time: Bad seconds value. Must be gt 0 and lt 60");

Error Time::Parse8601(const char* str, size_t len) {
	//	Year:
	//		YYYY (eg 1997)				- Not supported
	//	Year and month:
	//		YYYY-MM (eg 1997-07)		- Not supported
	//	Complete date:
	//		YYYY-MM-DD (eg 1997-07-16)
	//	Complete date plus hours and minutes:
	//		YYYY-MM-DDThh:mmTZD (eg 1997-07-16T19:20+01:00)
	//	Complete date plus hours, minutes and seconds:
	//		YYYY-MM-DDThh:mm:ssTZD (eg 1997-07-16T19:20:30+01:00)
	//	Complete date plus hours, minutes, seconds and a decimal fraction of a second:
	//		YYYY-MM-DDThh:mm:ss.sTZD (eg 1997-07-16T19:20:30.45+01:00)

	if (len < 10)
		return Err8601DateParse;

	int    year = 0, month = 0, day = 0, h = 0, m = 0, tzh = 0, tzm = 0;
	double s = 0.0;

	int numParsed = sscanf(str, "%d-%d-%dT%d:%d:%lf%d:%dZ", &year, &month, &day, &h, &m, &s, &tzh, &tzm);
	if (numParsed < 4 && len > 11 && str[10] == ' ') {
		// try without the T (YYYY-MM-DD hh:mm:ss)
		numParsed = sscanf(str, "%d-%d-%d %d:%d:%lf%d:%dZ", &year, &month, &day, &h, &m, &s, &tzh, &tzm);
	}
	if (numParsed < 3) {
		// try colons in the date (YYYY:MM:DD hh:mm:ss)
		numParsed = sscanf(str, "%d:%d:%d %d:%d:%lf%d:%dZ", &year, &month, &day, &h, &m, &s, &tzh, &tzm);
	}
	if (numParsed < 3)
		return Err8601DateParse;

	if (year < 1000 || year > 9999)
		return Err8601BadYear;
	if (month < 1 || month > 12)
		return Err8601BadMonth;
	if (day < 1 || day > 32)
		return Err8601BadDay;

	if (numParsed > 3) {
		if (h < 0 || h > 23)
			return Err8601BadHour;
		if (m < 0 || m > 59)
			return Err8601BadMinute;
		if (s < 0 || !(s < 60.0))
			return Err8601BadSecond;
	}
	if (numParsed > 6) {
		int a = abs(tzh);
		if (a < 0 || a > 59)
			return Err8601BadHour;
		if (tzm < 0 || tzm > 59)
			return Err8601BadMinute;

		// Timezone included
		if (tzh < 0)
			tzm = -tzm;
	}

	double intpart, fractpart;
	fractpart = modf(s, &intpart);
	*this     = Time(year, (time::Month) month, day, h, m, static_cast<int>(intpart), static_cast<int>(round(fractpart * 1000000000)));
	*this -= (tzh * 60 + tzm) * Minute;
	return Error();
}

Error Time::Parse8601(const std::string& str) {
	return Parse8601(str.c_str(), str.size());
}

void Time::FormatHttp(char* buf) const {
	int         year, day, yday;
	time::Month month;
	DateComponents(year, month, day, yday);

	int hour, min, sec;
	TimeComponents(hour, min, sec);

	char sday[4];
	memcpy(sday, WeekDayTable[(int) Weekday()], 3);
	sday[3] = 0;

	char smon[4];
	memcpy(smon, MonthTable[(int) month - 1], 3);
	smon[3] = 0;

	// Sun, 06 Nov 1994 08:49:37 GMT
	sprintf(buf, "%s, %02d %s %04d %02d:%02d:%02d GMT", sday, day, smon, year, hour, min, sec);
}

static StaticError ErrHttpDateParse("Invalid HTTP date format. Must be like \"Sun, 06 Nov 1994 08:49:37 GMT\"");

Error Time::ParseHttp(const char* str, size_t len) {
	// 0         10        20
	// 01234567890123456789012345678
	// Sun, 06 Nov 1994 08:49:37 GMT
	if (len != 29)
		return ErrHttpDateParse;
	int  dmon = AtoILen<char, 2>(str + 5);
	int  year = AtoILen<char, 4>(str + 12);
	int  h    = AtoILen<char, 2>(str + 17);
	int  m    = AtoILen<char, 2>(str + 20);
	int  s    = AtoILen<char, 2>(str + 23);
	auto mon  = TMonthFromName(3, str + 8);
	if (!(str[26] == 'G' && str[27] == 'M' && str[28] == 'T'))
		return ErrHttpDateParse;
	*this = Time(year, mon, dmon, h, m, s, 0);
	return Error();
}

int64_t Time::SubMicro(Time t) const {
	int64_t m = (Sec - t.Sec) * 1000000;
	m += Nsec / 1000 - t.Nsec / 1000;
	return m;
}

Time Time::PlusMicro(int64_t micro) const {
	int64_t newSec   = Sec;
	int64_t newMicro = (int64_t) Nsec / 1000 + micro;
	Normalize(newSec, newMicro, (int64_t) 1000000);
	return Time::FromInternal(newSec, (int32_t)(newMicro * 1000));
}

int Time::Compare(Time t) const {
	if (Sec == t.Sec) {
		if (Nsec < t.Nsec)
			return -1;
		if (Nsec > t.Nsec)
			return 1;
		return 0;
	}

	if (Sec < t.Sec)
		return -1;
	if (Sec > t.Sec)
		return 1;
	return 0;
}

void Time::AddNano(int64_t nsec) {
	int64_t newSec  = Sec;
	int64_t newNsec = (int64_t) Nsec + nsec;
	Normalize(newSec, newNsec, (int64_t) 1000000000);
	Sec  = newSec;
	Nsec = (int32_t) newNsec;
}

Time Time::PlusNano(int64_t nsec) const {
	Time copy = *this;
	copy.AddNano(nsec);
	return copy;
}

Duration Time::Sub(Time d) const {
	int64_t n = (Sec - d.Sec) * 1000000000;
	n += Nsec - d.Nsec;
	return n * Nanosecond;
}

Time Time::FromInternal(int64_t sec, int32_t nsec) {
	IMQS_ASSERT((uint32_t) nsec < 1000000000);
	Time d;
	d.Sec  = sec;
	d.Nsec = nsec;
	return d;
}

bool Time::operator<(Time t) const {
	if (Sec == t.Sec)
		return Nsec < t.Nsec;
	return Sec < t.Sec;
}

bool Time::operator<=(Time t) const {
	if (Sec == t.Sec)
		return Nsec <= t.Nsec;
	return Sec <= t.Sec;
}

bool Time::operator>(Time t) const {
	if (Sec == t.Sec)
		return Nsec > t.Nsec;
	return Sec > t.Sec;
}

bool Time::operator>=(Time t) const {
	if (Sec == t.Sec)
		return Nsec >= t.Nsec;
	return Sec >= t.Sec;
}

IMQS_PAL_API Time Now() {
	return Time::Now();
}

int64_t PerformanceCounter() {
#ifdef _WIN32
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
#else
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t) t.tv_sec * (uint64_t) 1000000000 + (uint64_t) t.tv_nsec;
#endif
}

int64_t PerformanceFrequency() {
#ifdef _WIN32
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	return f.QuadPart;
#else
	return 1000000000;
#endif
}
} // namespace time
} // namespace imqs
