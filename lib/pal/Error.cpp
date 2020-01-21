#include "pch.h"

namespace imqs {

static IMQS_NORETURN void Die() {
	IMQS_DEBUG_BREAK();
#ifdef __clang__
	__builtin_trap();
#else
	*((int*) 0) = 1;
#endif
}

static const char* NoErrorMsg = "";

StaticError::StaticError(const char* msg) {
	Msg = msg;
}

Error::Error(const Error& e) {
	if (e.IsStatic() || e.Msg == 0)
		Msg = e.Msg;
	else
		Set(e.MsgString());
}

Error::Error(const char* msg) {
	Set(msg);
}

Error::Error(const std::string& msg) : Error(msg.c_str()) {
}

// This doesn't seem to be meaningful
//bool Error::operator==(const Error& e) const
//{
//	if (IsStatic() && e.IsStatic())
//		return Msg == e.Msg;
//	return strcmp(MsgString(), e.MsgString()) == 0;
//}

bool Error::operator==(const StaticError& e) const {
	if (IsStatic())
		return (Msg & ~1) == reinterpret_cast<uintptr_t>(&e);
	return false;
}

Error& Error::operator=(const Error& e) {
	Free();
	if (e.IsStatic() || e.Msg == 0)
		Msg = e.Msg;
	else
		Set(e.Message());

	return *this;
}

void Error::Set(const char* msg) {
	char* copy = (char*) malloc(strlen(msg) + 1);
	if (!copy)
		Die();

	strcpy(copy, msg);
	Msg = reinterpret_cast<uintptr_t>(copy);
}

const char* Error::MsgString() const {
	if (Msg == 0)
		return NoErrorMsg;

	if (IsStatic())
		return reinterpret_cast<StaticError*>(Msg & ~1)->Msg;
	else
		return reinterpret_cast<const char*>(Msg);
}

} // namespace imqs
