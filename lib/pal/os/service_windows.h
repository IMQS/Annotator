#pragma once
#ifdef _WIN32

#include <functional>
#include <WinSvc.h>

namespace imqs {
namespace os {

// Helper functions for working as a Windows Service
// We only support SERVICE_WIN32_OWN_PROCESS.

enum class WinService_StartupType : DWORD {
	NoChange = SERVICE_NO_CHANGE,
	Boot     = SERVICE_BOOT_START,
	System   = SERVICE_SYSTEM_START,
	Auto     = SERVICE_AUTO_START,
	Demand   = SERVICE_DEMAND_START,
	Disabled = SERVICE_DISABLED,
};

enum class WinService_Status : DWORD {
	Stopped          = SERVICE_STOPPED,
	Start_Pending    = SERVICE_START_PENDING,
	Stop_Pending     = SERVICE_STOP_PENDING,
	Running          = SERVICE_RUNNING,
	Continue_Pending = SERVICE_CONTINUE_PENDING,
	Pause_Pending    = SERVICE_PAUSE_PENDING,
	Paused           = SERVICE_PAUSED,
};

/* All of the state of your running service

	Instructions
	------------

	* Create an instance of WinService_State
	* Set SvcMain, and inside your SvcMain function, do the following:
		* Call SvcMain_Start(). Return immediately if SvcMain_Start() returns false.
		* Run your startup code.
		* Call ReportSvcStatus() to inform Windows that you are in the WinService_Status::Running state.
		* Run until SvcStopEvent is signaled.
		* Call SvcMain_End().
	* (Optionally set SvcCtrlHandler, if DefaultSvcCtrlHandler is not sufficient for your needs)
	* (Optionally set SvcStopEvent)
	* (Optionally set RunInForeground)
	* Call Run()

*/
class IMQS_PAL_API WinService_State {
public:
#ifdef UNICODE
	typedef std::wstring nstring;
#else
	typedef std::string nstring;
#endif

	// Callback function that you must provide
	void(WINAPI* SvcMain)(DWORD dwArgc, TCHAR** lpszArgv);

	// Called by SCM whenever a control code is sent to the service using the ControlService function.
	// If you do not set this, then it defaults to DefaultSvcCtrlHandler.
	// lpContext points to the WinService_State object
	DWORD(WINAPI* SvcCtrlHandler)
	(DWORD dwCtrl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext);

	// A callback function that is invoked when the service is stopped. This can be
	// used in addition to SvcStopEvent, to detect when the service receives the stop signal from the OS.
	std::function<void()> OnStop;

	nstring               SvcName;         // You can leave this blank for a SERVICE_WIN32_OWN_PROCESS
	SERVICE_STATUS_HANDLE SvcStatusHandle; // Populated by SvcMain_Start(). ReportSvcStatus uses this to inform the OS of the state of the service.

	// This is created by SvcMain_Start(), if NULL. DefaultSvcCtrlHandler() will toggle this upon receipt of SERVICE_CONTROL_STOP.
	// If this is not NULL when SvcMain_Start() is run, then we assume that you have set this to a handle of your own creation.
	HANDLE SvcStopEvent;

	DWORD       Win32ExitCode; // Sent to SetServiceStatus() every time ReportSvcStatus is called. Default = 0.
	std::string LastError;     // If an error occurs inside any function in here, it gets written to LastError

	// If true, then do not run as a proper Windows Service.
	// Instead, run in the foreground. This turns a lot of calls into no-ops.
	// This is useful because it allows you to use a single code path for "service" as well as "foreground" execution.
	// If Ctrl+C is pressed when running in this mode, then SvcStopEvent will be signaled, allowing you to test
	// graceful shutdown when running in the foreground.
	bool RunInForeground;

	WinService_State();
	~WinService_State();
	bool Run();                                                               // If this returns false, then the error is in LastError.
	bool SvcMain_Start(DWORD dwArgc, TCHAR** lpszArgv);                       // Call this at the start of your SvcMain. If it returns false, then return immediately from your SvcMain().
	void SvcMain_End();                                                       // Call this at the end of your SvcMain
	void ReportSvcStatus(WinService_Status currentState, DWORD dwWaitHintMS); // Use this to inform the OS of the status of your service

	// Default SvcCtrlHandler. The only logic this performs is when receiving SERVICE_CONTROL_STOP, it will toggle SvcStopEvent.
	static DWORD WINAPI DefaultSvcCtrlHandler(DWORD dwCtrl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext);

	// Helper function that returns true if the Window Station is visible.
	// If this returns false, then it is likely that you are being launched
	// by the service controller.
	static bool IsWindowStationVisible();

protected:
	DWORD StatusCheckPoint;
	bool  OwnSvcStopEvent;
	void  ResetStatus();
	void  SetStopped();

	static BOOL WINAPI ConsoleHandler(DWORD CtrlType);
};
} // namespace os
} // namespace imqs

#endif // _WIN32
