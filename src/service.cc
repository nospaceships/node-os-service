#ifndef SERVICE_CC
#define SERVICE_CC

#include <node.h>
#include <nan.h>

#include <io.h>
#include <signal.h>
#include <stdio.h>
#include <string>
#include <windows.h>

#include "pthread.h"
#include "service.h"

static bool run_initialised = false;

static SERVICE_STATUS_HANDLE status_handle;
static pthread_mutex_t status_handle_mtx;

static bool stop_requested = false;
static pthread_mutex_t stop_requested_mtx;

static pthread_cond_t stop_service;
static pthread_mutex_t stop_service_mtx;

HANDLE node_thread_handle;

static char errbuf[1024];
const char* raw_strerror (int code) {
	if (FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM, 0, code, 0, errbuf,
			1024, NULL)) {
		return errbuf;
	} else {
		strcpy (errbuf, "Unknown error");
		return errbuf;
	}
}

DWORD set_status (DWORD status_code, DWORD win32_rcode, DWORD service_rcode) {
	SERVICE_STATUS status;

	memset (&status, 0, sizeof (SERVICE_STATUS));

	status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	status.dwCurrentState = status_code;
	status.dwControlsAccepted = SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_STOP;
	status.dwWin32ExitCode = (service_rcode
			? ERROR_SERVICE_SPECIFIC_ERROR
			: win32_rcode);
	status.dwServiceSpecificExitCode = service_rcode;
	status.dwCheckPoint = 0;
	status.dwWaitHint = 10000;

	pthread_mutex_lock (&status_handle_mtx);
	SetServiceStatus (status_handle, &status);
	pthread_mutex_unlock (&status_handle_mtx);

	return 0;
}

VOID WINAPI handler (DWORD signal) {
	switch (signal) {
		case (SERVICE_CONTROL_STOP):
		case (SERVICE_CONTROL_SHUTDOWN):
			set_status(SERVICE_STOP_PENDING, NO_ERROR, 0);
			pthread_mutex_lock (&stop_requested_mtx);
			stop_requested = true;
			pthread_mutex_unlock (&stop_requested_mtx);
			break;
		default:
			break;
	}
}

VOID WINAPI run (DWORD argc, LPTSTR *argv) {
	if (! (status_handle = RegisterServiceCtrlHandler ("", handler))) {
		set_status (SERVICE_STOPPED, GetLastError (), 0);
		return;
	}

	set_status (SERVICE_RUNNING, NO_ERROR, 0);

	pthread_mutex_lock (&stop_service_mtx);
	pthread_cond_wait (&stop_service, &stop_service_mtx);
	pthread_mutex_unlock (&stop_service_mtx);
}

DWORD __stdcall run_thread (LPVOID param) {
	SERVICE_TABLE_ENTRY table[] = {{"", run}, {0, }};

	if (StartServiceCtrlDispatcher (table)) {
		while (1) {
			DWORD status;
			BOOL rcode = GetExitCodeThread (node_thread_handle, &status);

			if (! rcode)
				break;

			if (status != STILL_ACTIVE)
				break;
		}
	} else {
		while (1) {
			Sleep(60000);
		}
	}

	ExitThread (0);
}

namespace service {

void InitAll (Handle<Object> exports) {
	pthread_mutex_init(&status_handle_mtx, NULL);
	pthread_mutex_init(&stop_requested_mtx, NULL);
	pthread_mutex_init(&stop_service_mtx, NULL);
	
	pthread_cond_init(&stop_service, NULL);

	exports->Set(Nan::New("add").ToLocalChecked(),
			Nan::GetFunction(Nan::New<FunctionTemplate>(Add)).ToLocalChecked());

	exports->Set(Nan::New("isStopRequested").ToLocalChecked(),
			Nan::GetFunction(Nan::New<FunctionTemplate>(IsStopRequested)).ToLocalChecked());
	
	exports->Set(Nan::New("remove").ToLocalChecked(),
			Nan::GetFunction(Nan::New<FunctionTemplate>(Remove)).ToLocalChecked());
	
	exports->Set(Nan::New("run").ToLocalChecked(),
			Nan::GetFunction(Nan::New<FunctionTemplate>(Run)).ToLocalChecked());
	
	exports->Set(Nan::New("stop").ToLocalChecked(),
			Nan::GetFunction(Nan::New<FunctionTemplate>(Stop)).ToLocalChecked());
}

NODE_MODULE(service, InitAll)

NAN_METHOD(Add) {
	Nan::HandleScope scope;
	
	if (info.Length() < 3) {
		Nan::ThrowError("Two arguments are required");
		return;
	}
	
	if (! info[0]->IsString()) {
		Nan::ThrowTypeError("Name argument must be a string");
		return;
	}
	
	Nan::Utf8String name(info[0]);

	if (! info[1]->IsString ()) {
		Nan::ThrowTypeError("Display name argument must be a string");
		return;
	}
	
	Nan::Utf8String display_name(info[1]);

	if (! info[2]->IsString ()) {
		Nan::ThrowTypeError("Name argument must be a string");
		return;
	}
	
	Nan::Utf8String path(info[2]);

	SC_HANDLE scm_handle = OpenSCManager (0, SERVICES_ACTIVE_DATABASE,
			SC_MANAGER_ALL_ACCESS);
	if (! scm_handle) {
		std::string message ("OpenSCManager() failed: ");
		message += raw_strerror (GetLastError ());
		Nan::ThrowError(message.c_str());
		return;
	}

	SC_HANDLE svc_handle = CreateService (scm_handle, *name, *display_name,
			SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
			SERVICE_ERROR_NORMAL, *path, 0, 0, 0, 0, 0);

	if (! svc_handle) {
		std::string message ("CreateService() failed: ");
		message += raw_strerror (GetLastError ());
		CloseServiceHandle (scm_handle);
		Nan::ThrowError(message.c_str());
		return;
	}

	CloseServiceHandle (svc_handle);
	CloseServiceHandle (scm_handle);

	info.GetReturnValue().Set(info.This());
}

NAN_METHOD(IsStopRequested) {
	Nan::HandleScope scope;

	pthread_mutex_lock (&stop_requested_mtx);
	bool requested = stop_requested ? true : false;
	stop_requested = false;
	pthread_mutex_unlock (&stop_requested_mtx);
	
	info.GetReturnValue().Set(requested);
}

NAN_METHOD(Remove) {
	Nan::HandleScope scope;
	
	if (info.Length () < 1) {
		Nan::ThrowError("One argument is required");
		return;
	}
	
	if (! info[0]->IsString ()) {
		Nan::ThrowTypeError("Name argument must be a string");
		return;
	}
	
	Nan::Utf8String name(info[0]);

	SC_HANDLE scm_handle = OpenSCManager (0, SERVICES_ACTIVE_DATABASE,
			SC_MANAGER_ALL_ACCESS);
	if (! scm_handle) {
		std::string message ("OpenSCManager() failed: ");
		message += raw_strerror (GetLastError ());
		Nan::ThrowError(message.c_str());
		return;
	}

	SC_HANDLE svc_handle = OpenService (scm_handle, *name, SERVICE_ALL_ACCESS);
	if (! svc_handle) {
		std::string message ("OpenService() failed: ");
		message += raw_strerror (GetLastError ());
		CloseServiceHandle (scm_handle);
		Nan::ThrowError(message.c_str());
		return;
	}

	if (! DeleteService (svc_handle)) {
		std::string message ("DeleteService() failed: ");
		message += raw_strerror (GetLastError ());
		CloseServiceHandle (svc_handle);
		CloseServiceHandle (scm_handle);
		Nan::ThrowError(message.c_str());
		return;
	}

	CloseServiceHandle (svc_handle);
	CloseServiceHandle (scm_handle);

	info.GetReturnValue().Set(info.This());
}

NAN_METHOD(Run) {
	Nan::HandleScope scope;
	
	if (run_initialised) {
		info.GetReturnValue().Set(info.This());
		return;
	}

	node_thread_handle = GetCurrentThread ();

	HANDLE handle = CreateThread (NULL, 0, run_thread, NULL, 0, NULL);
	if (! handle) {
		std::string message ("CreateThread() failed: ");
		message += raw_strerror (GetLastError ());
		Nan::ThrowError(message.c_str());
		return;
	} else {
		CloseHandle (handle);
	}
	
	run_initialised = true;

	info.GetReturnValue().Set(info.This());
}

NAN_METHOD(Stop) {
	Nan::HandleScope scope;
	
	if (! run_initialised) {
		info.GetReturnValue().Set(info.This());
		return;
	}
	
	int rcode = 0;
	
	if (info.Length () > 1) {
		if (! info[0]->IsUint32 ()) {
			Nan::ThrowTypeError("Name argument must be a string");
			return;
		}
		
		rcode = info[0]->ToUint32()->Value();
	}
	
	set_status (SERVICE_STOP_PENDING, NO_ERROR, 0);
	
	pthread_cond_signal (&stop_service);
	
	set_status (SERVICE_STOPPED, NO_ERROR, rcode);

	info.GetReturnValue().Set(info.This());
}

}; /* namespace service */

#endif /* SERVICE_CC */
