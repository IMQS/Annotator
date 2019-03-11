#include "pch.h"
#include "service_windows.h"

#pragma comment(lib, "advapi32.lib")

namespace imqs {
namespace os {

static WinService_State* SingleRunningService;

static std::string SysErrMsg(DWORD err) {
	char   szBuf[1024];
	LPVOID lpMsgBuf;

	FormatMessageA(
	    FORMAT_MESSAGE_ALLOCATE_BUFFER |
	        FORMAT_MESSAGE_FROM_SYSTEM,
	    nullptr,
	    err,
	    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
	    (LPSTR) &lpMsgBuf,
	    0, nullptr);

	sprintf(szBuf, "(%u) %s", err, (const char*) lpMsgBuf);
	LocalFree(lpMsgBuf);

	// chop off trailing carriage returns
	std::string r = szBuf;
	while (r.length() > 0 && (r[r.length() - 1] == 10 || r[r.length() - 1] == 13))
		r.resize(r.length() - 1);

	return r;
}

static std::string SysLastErrMsg() {
	return SysErrMsg(GetLastError());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

WinService_State::WinService_State() {
	ResetStatus();
}

WinService_State::~WinService_State() {
}

void WinService_State::ResetStatus() {
	SvcStatusHandle  = nullptr;
	SvcStopEvent     = nullptr;
	SvcMain          = nullptr;
	SvcCtrlHandler   = nullptr;
	OwnSvcStopEvent  = false;
	StatusCheckPoint = 0;
	Win32ExitCode    = 0;
	RunInForeground  = false;
}

bool WinService_State::Run() {
	if (SvcMain == nullptr) {
		LastError = "SvcMain must be set";
		return false;
	}

	if (RunInForeground) {
		SingleRunningService = this;
		SetConsoleCtrlHandler(&ConsoleHandler, true);
		SvcMain(0, nullptr);
		return true;
	}

	SERVICE_TABLE_ENTRY dispatchTable[] =
	    {
	        {TEXT(""), (LPSERVICE_MAIN_FUNCTION) SvcMain}, // name is ignored for SERVICE_WIN32_OWN_PROCESS
	        {nullptr, nullptr}};

	if (!StartServiceCtrlDispatcher(dispatchTable)) {
		LastError = "StartServiceCtrlDispatcher failed with " + SysLastErrMsg();
		return false;
	}

	return true;
}

void WinService_State::SetStopped() {
	SetEvent(SvcStopEvent);
	if (OnStop)
		OnStop();
}

// The arguments here are NOT the regular command-line arguments.
// It is special arguments that the user can type in on the services control panel, or that another process can send us via StartService().
bool WinService_State::SvcMain_Start(DWORD dwArgc, TCHAR** lpszArgv) {
	if (dwArgc == 1)
		SvcName = lpszArgv[0];

	if (SvcStopEvent == nullptr) {
		SvcStopEvent = CreateEvent(nullptr, true, false, nullptr);
		if (SvcStopEvent == nullptr) {
			LastError = "Unable to create Service Stop Event: " + SysLastErrMsg();
			return false;
		}
		OwnSvcStopEvent = true;
	}

	if (!RunInForeground) {
		LPHANDLER_FUNCTION_EX ctrlHandler = SvcCtrlHandler ? SvcCtrlHandler : DefaultSvcCtrlHandler;
		SvcStatusHandle                   = RegisterServiceCtrlHandlerEx(SvcName.c_str(), ctrlHandler, this); // name is ignored for SERVICE_WIN32_OWN_PROCESS
		if (SvcStatusHandle == nullptr) {
			LastError = "Unable to do RegisterServiceCtrlHandlerEx: " + SysLastErrMsg();
			SetStopped();
			return false;
		}
	}

	ReportSvcStatus(WinService_Status::Start_Pending, 1000);

	return true;
}

void WinService_State::SvcMain_End() {
	ReportSvcStatus(WinService_Status::Stopped, 0);

	if (OwnSvcStopEvent) {
		CloseHandle(SvcStopEvent);
		SvcStopEvent    = nullptr;
		OwnSvcStopEvent = false;
	}
	CloseHandle(SvcStatusHandle);
	SvcStatusHandle = nullptr;

	if (SingleRunningService == this)
		SingleRunningService = nullptr;
}

// Sets the current service status and reports it to the SCM.
//	currentState	The current state (see SERVICE_STATUS)
//	dwWaitHintMS	Estimated time for pending operation, in milliseconds
void WinService_State::ReportSvcStatus(WinService_Status currentState, DWORD dwWaitHintMS) {
	if (RunInForeground)
		return;

	SERVICE_STATUS status;
	memset(&status, 0, sizeof(status));

	status.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
	status.dwCurrentState            = (DWORD) currentState;
	status.dwServiceSpecificExitCode = 0;
	status.dwWin32ExitCode           = Win32ExitCode;
	status.dwWaitHint                = dwWaitHintMS;

	if (currentState == WinService_Status::Start_Pending)
		status.dwControlsAccepted = 0;
	else
		status.dwControlsAccepted = SERVICE_ACCEPT_STOP;

	if (currentState == WinService_Status::Running || currentState == WinService_Status::Stopped)
		StatusCheckPoint = 0;
	else
		StatusCheckPoint++;

	status.dwCheckPoint = StatusCheckPoint;

	// Report the status of the service to the SCM.
	SetServiceStatus(SvcStatusHandle, &status);
}

DWORD WINAPI WinService_State::DefaultSvcCtrlHandler(DWORD dwCtrl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext) {
	WinService_State* self = (WinService_State*) lpContext;
	switch (dwCtrl) {
	case SERVICE_CONTROL_STOP:
		self->ReportSvcStatus(WinService_Status::Stop_Pending, 0);
		self->SetStopped();
		return NO_ERROR;

	case SERVICE_CONTROL_INTERROGATE:
		return NO_ERROR;
	}

	return ERROR_CALL_NOT_IMPLEMENTED;
}

bool WinService_State::IsWindowStationVisible() {
	HWINSTA hWinStation = GetProcessWindowStation();
	if (hWinStation) {
		USEROBJECTFLAGS uof = {0};
		if (GetUserObjectInformation(hWinStation, UOI_FLAGS, &uof, sizeof(USEROBJECTFLAGS), NULL) &&
		    ((uof.dwFlags & WSF_VISIBLE) == 0)) {
			return false;
		}
	}
	return true;
}

BOOL WINAPI WinService_State::ConsoleHandler(DWORD CtrlType) {
	switch (CtrlType) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
		SingleRunningService->SetStopped();
		return true;
	}
	return false;
}
} // namespace os
} // namespace imqs