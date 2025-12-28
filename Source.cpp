//ServiceKiller32

#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <tchar.h>
#include <exdisp.h>
#include <exdispid.h>
#include <commctrl.h>
#include <winsvc.h>
#include <cstdio>
#include "resource.h"

#define WIN32_LEAN_AND_MEAN
#define WIN32_WINNT  0x0500
#define SZAPPNAME            "Simple"
#define SZSERVICENAME        "SimpleService"
#define SZSERVICEDISPLAYNAME "Simple Service"
#define SZDEPENDENCIES       ""
#pragma comment(lib,"Comctl32")
#pragma comment(lib,"Advapi32")


#define ConvertStringSecurityDescriptorToSecurityDescriptor ConvertStringSecurityDescriptorToSecurityDescriptorA
extern "C" {BOOL  __stdcall ConvertStringSecurityDescriptorToSecurityDescriptorA(LPCSTR StringSecurityDescriptor, DWORD StringSDRevision, PSECURITY_DESCRIPTOR  *SecurityDescriptor, PULONG  SecurityDescriptorSize OPTIONAL); }


static char *jours[7] = { "dimanche", "lundi","mardi","mercredi","jeudi","vendredi","samedi" };
static char *mois[12] = { "janvier", "février","mars", "avril", "mai", "juin","juillet","aout","septembre", "octobre", "novembre", "décembre" };
TCHAR buffer[4096];
HANDLE  hServerStopEvent = NULL;
SERVICE_STATUS          ssStatus;
SERVICE_STATUS_HANDLE   sshStatusHandle;
DWORD                   dwErr = 0;
BOOL                    bDebug = FALSE;
TCHAR                   szErr[256];
NOTIFYICONDATAA nf;
WNDCLASS wc;
HINSTANCE hInst;
LPDRAWITEMSTRUCT lpdis;
SYSTEMTIME st;
HWND hList, hMain;
ENUM_SERVICE_STATUS EnService[512];
SC_HANDLE ScManager, ScService;
LPQUERY_SERVICE_CONFIG lpqscBuf;
SYSTEMTIME st;
SC_HANDLE   schService;
SC_HANDLE   schSCManager;
SERVICE_STATUS          ssStatus;
SERVICE_STATUS_HANDLE   sshStatusHandle;
DWORD                   dwErr = 0;
TCHAR                   szErr[256];

void ErrorDescription(DWORD p_dwError) {

	HLOCAL hLocal = NULL;

	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
		NULL, p_dwError, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPTSTR)&hLocal,
		0, NULL);

	MessageBox(NULL, (LPCTSTR)LocalLock(hLocal), TEXT("Error"), MB_OK | MB_ICONERROR);
	LocalFree(hLocal);
}
int enumservices();
static BOOL CALLBACK DialogFunc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
VOID ServiceStart(DWORD dwArgc, LPTSTR *lpszArgv);
VOID ServiceStop();
BOOL ReportStatusToSCMgr(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint);
BOOL CreateMyDACL(SECURITY_ATTRIBUTES * pSA);
void AddToMessageLog(LPTSTR lpszMsg);
VOID WINAPI service_ctrl(DWORD dwCtrlCode);
VOID WINAPI service_main(DWORD dwArgc, LPTSTR *lpszArgv);
VOID CmdInstallService();
VOID CmdRemoveService();
VOID CmdDebugService(int argc, char **argv);
BOOL WINAPI ControlHandler(DWORD dwCtrlType);
LPTSTR GetLastErrorText(LPTSTR lpszBuf, DWORD dwSize);
int APIENTRY KillMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR     lpCmdLine, int       nCmdShow);
static BOOL CALLBACK DialogKillFunc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

void WINAPI service_main(DWORD dwArgc, LPTSTR *lpszArgv) {
	sshStatusHandle = RegisterServiceCtrlHandler(TEXT(SZSERVICENAME), service_ctrl);
	if (!sshStatusHandle) goto cleanup;
	ssStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	ssStatus.dwServiceSpecificExitCode = 0;
	if (!ReportStatusToSCMgr(SERVICE_START_PENDING, NO_ERROR, 3000))    goto cleanup;
	ServiceStart(dwArgc, lpszArgv);
cleanup:
	if (sshStatusHandle) (VOID)ReportStatusToSCMgr(SERVICE_STOPPED, dwErr, 0);
	return;
}
VOID WINAPI service_ctrl(DWORD dwCtrlCode) {
	switch (dwCtrlCode) {
	case SERVICE_CONTROL_STOP:      ReportStatusToSCMgr(SERVICE_STOP_PENDING, NO_ERROR, 0);   ServiceStop();      return;
	case SERVICE_CONTROL_INTERROGATE:break;
	default:   break;

	}
	ReportStatusToSCMgr(ssStatus.dwCurrentState, NO_ERROR, 0);
}
BOOL ReportStatusToSCMgr(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint) {
	static DWORD dwCheckPoint = 1;
	BOOL fResult = TRUE;
	if (!bDebug) {
		if (dwCurrentState == SERVICE_START_PENDING) ssStatus.dwControlsAccepted = 0; else   ssStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
		ssStatus.dwCurrentState = dwCurrentState;
		ssStatus.dwWin32ExitCode = dwWin32ExitCode;
		ssStatus.dwWaitHint = dwWaitHint;
		if ((dwCurrentState == SERVICE_RUNNING) || (dwCurrentState == SERVICE_STOPPED))ssStatus.dwCheckPoint = 0;
		else
			ssStatus.dwCheckPoint = dwCheckPoint++;
		if (!(fResult = SetServiceStatus(sshStatusHandle, &ssStatus))) { AddToMessageLog(TEXT("SetServiceStatus")); }
	}
	return fResult;
}
VOID AddToMessageLog(LPTSTR lpszMsg) {
	TCHAR szMsg[(sizeof(SZSERVICENAME) / sizeof(TCHAR)) + 100];
	HANDLE  hEventSource;
	LPTSTR  lpszStrings[2];
	if (!bDebug) {
		dwErr = GetLastError();
		hEventSource = RegisterEventSource(NULL, TEXT(SZSERVICENAME));
		_stprintf(szMsg, TEXT("%s error: %d"), TEXT(SZSERVICENAME), dwErr);
		lpszStrings[0] = szMsg;
		lpszStrings[1] = lpszMsg;
		if (hEventSource != NULL) {
			ReportEvent(hEventSource, EVENTLOG_ERROR_TYPE, 0, 0, NULL, 2, 0, (const char**)lpszStrings, NULL);
			(VOID)DeregisterEventSource(hEventSource);
		}
	}
}
void CmdInstallService() {
	SC_HANDLE   schService;
	SC_HANDLE   schSCManager;
	TCHAR szPath[512];
	if (GetModuleFileName(NULL, szPath, 512) == 0) { _tprintf(TEXT("Unable to install %s - %s\n"), TEXT(SZSERVICEDISPLAYNAME), GetLastErrorText(szErr, 256)); return; }
	schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
	if (schSCManager) {
		schService = CreateService(schSCManager, TEXT(SZSERVICENAME), TEXT(SZSERVICEDISPLAYNAME), SERVICE_QUERY_STATUS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, szPath, NULL, NULL, TEXT(SZDEPENDENCIES), NULL, NULL);
		if (schService) {
			_tprintf(TEXT("%s installed.\n"), TEXT(SZSERVICEDISPLAYNAME));
			CloseServiceHandle(schService);
		}
		else {
			_tprintf(TEXT("CreateService failed - %s\n"), GetLastErrorText(szErr, 256));
		}
		CloseServiceHandle(schSCManager);
	}
	else
		_tprintf(TEXT("OpenSCManager failed - %s\n"), GetLastErrorText(szErr, 256));
}
void CmdRemoveService() {
	SC_HANDLE   schService;
	SC_HANDLE   schSCManager;
	schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
	if (schSCManager) {
		schService = OpenService(schSCManager, TEXT(SZSERVICENAME), DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);
		if (schService) {
			if (ControlService(schService, SERVICE_CONTROL_STOP, &ssStatus)) {
				_tprintf(TEXT("Stopping %s."), TEXT(SZSERVICEDISPLAYNAME)); Sleep(1000);

				while (QueryServiceStatus(schService, &ssStatus)) {
					if (ssStatus.dwCurrentState == SERVICE_STOP_PENDING) { _tprintf(TEXT(".")); Sleep(1000); }
					else                  break;
				}
				if (ssStatus.dwCurrentState == SERVICE_STOPPED) _tprintf(TEXT("\n%s stopped.\n"), TEXT(SZSERVICEDISPLAYNAME));
				else
					_tprintf(TEXT("\n%s failed to stop.\n"), TEXT(SZSERVICEDISPLAYNAME));
			}
			if (DeleteService(schService))
				_tprintf(TEXT("%s removed.\n"), TEXT(SZSERVICEDISPLAYNAME));
			else
				_tprintf(TEXT("DeleteService failed - %s\n"), GetLastErrorText(szErr, 256));


			CloseServiceHandle(schService);
		}
		else
			_tprintf(TEXT("OpenService failed - %s\n"), GetLastErrorText(szErr, 256));

		CloseServiceHandle(schSCManager);
	}
	else
		_tprintf(TEXT("OpenSCManager failed - %s\n"), GetLastErrorText(szErr, 256));
}
void CmdDebugService(int argc, char ** argv) {
	DWORD dwArgc;   LPTSTR *lpszArgv;
#ifdef UNICODE
	lpszArgv = CommandLineToArgvW(GetCommandLineW(), &(dwArgc));
	if (NULL == lpszArgv) { _tprintf(TEXT("CmdDebugService CommandLineToArgvW returned NULL\n"));       return; }
#else
	dwArgc = (DWORD)argc;   lpszArgv = argv;
#endif
	_tprintf(TEXT("Debugging %s.\n"), TEXT(SZSERVICEDISPLAYNAME));
	SetConsoleCtrlHandler(ControlHandler, TRUE);
	ServiceStart(dwArgc, lpszArgv);
#ifdef UNICODE
	GlobalFree(lpszArgv);
#endif

}
BOOL WINAPI ControlHandler(DWORD dwCtrlType) {
	switch (dwCtrlType) {
	case CTRL_BREAK_EVENT:
	case CTRL_C_EVENT:
		_tprintf(TEXT("Stopping %s.\n"), TEXT(SZSERVICEDISPLAYNAME));
		ServiceStop();
		return TRUE;
		break;

	}
	return FALSE;
}
LPTSTR GetLastErrorText(LPTSTR lpszBuf, DWORD dwSize) {
	DWORD dwRet;
	LPTSTR lpszTemp = NULL;
	dwRet = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ARGUMENT_ARRAY, NULL, GetLastError(), LANG_NEUTRAL, (LPTSTR)&lpszTemp, 0, NULL);
	if (!dwRet || ((long)dwSize < (long)dwRet + 14))
		lpszBuf[0] = TEXT('\0');
	else
	{
		lpszTemp[lstrlen(lpszTemp) - 2] = TEXT('\0');
		_stprintf(lpszBuf, TEXT("%s (0x%x)"), lpszTemp, GetLastError());
	}

	if (lpszTemp)LocalFree((HLOCAL)lpszTemp);
	return lpszBuf;
}
BOOL CreateMyDACL(SECURITY_ATTRIBUTES * pSA) {
	TCHAR * szSD = "D:""(D;OICI;GA;;;BG)""(D;OICI;GA;;;AN)""(A;OICI;GRGWGX;;;AU)""(A;OICI;GA;;;BA)";
	if (NULL == pSA) return FALSE;
	return ConvertStringSecurityDescriptorToSecurityDescriptor(szSD, SDDL_REVISION_1, &(pSA->lpSecurityDescriptor), NULL);
}
VOID ServiceStart(DWORD dwArgc, LPTSTR *lpszArgv) {
	HANDLE                  hPipe = INVALID_HANDLE_VALUE;
	HANDLE                  hEvents[2] = { NULL, NULL };
	OVERLAPPED              os;
	PSECURITY_DESCRIPTOR    pSD = NULL;
	SECURITY_ATTRIBUTES     sa;
	TCHAR                   szIn[80];
	TCHAR                   szOut[(sizeof(szIn) / sizeof(TCHAR)) + 100];
	LPTSTR                  lpszPipeName = TEXT("\\\\.\\pipe\\simple");
	BOOL                    bRet;
	DWORD                   cbRead;
	DWORD                   cbWritten;
	DWORD                   dwWait;
	UINT                    ndx;
	if (!ReportStatusToSCMgr(SERVICE_START_PENDING, NO_ERROR, 3000))  goto cleanup;
	hServerStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (hServerStopEvent == NULL) goto cleanup;
	hEvents[0] = hServerStopEvent;
	if (!ReportStatusToSCMgr(SERVICE_START_PENDING, NO_ERROR, 3000)) goto cleanup;
	hEvents[1] = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (hEvents[1] == NULL) goto cleanup;
	if (!ReportStatusToSCMgr(SERVICE_START_PENDING, NO_ERROR, 3000)) goto cleanup;
	pSD = (PSECURITY_DESCRIPTOR)malloc(SECURITY_DESCRIPTOR_MIN_LENGTH);
	if (pSD == NULL) goto cleanup;
	if (!InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION)) goto cleanup;
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = pSD;
	if (!CreateMyDACL(&sa)) { return; }
	if (!ReportStatusToSCMgr(SERVICE_START_PENDING, NO_ERROR, 3000)) goto cleanup;
	for (ndx = 1; ndx < dwArgc - 1; ndx++) {
		if (((*(lpszArgv[ndx]) == TEXT('-')) || (*(lpszArgv[ndx]) == TEXT('/'))) && (!_tcsicmp(TEXT("pipe"), lpszArgv[ndx] + 1) && ((ndx + 1) < dwArgc))) {
			lpszPipeName = lpszArgv[++ndx];
		}
	}
	hPipe = CreateNamedPipe(lpszPipeName, FILE_FLAG_OVERLAPPED | PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 0, 0, 1000, &sa);
	if (hPipe == INVALID_HANDLE_VALUE) { AddToMessageLog(TEXT("Unable to create named pipe"));    goto cleanup; }
	if (!ReportStatusToSCMgr(SERVICE_RUNNING, NO_ERROR, 0)) goto cleanup;
	while (1) {
		memset(&os, 0, sizeof(OVERLAPPED));
		os.hEvent = hEvents[1];
		ResetEvent(hEvents[1]);
		ConnectNamedPipe(hPipe, &os);
		if (GetLastError() == ERROR_IO_PENDING) {
			dwWait = WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);
			if (dwWait != WAIT_OBJECT_0 + 1) break;
		}
		memset(&os, 0, sizeof(OVERLAPPED));
		os.hEvent = hEvents[1];
		ResetEvent(hEvents[1]);
		bRet = ReadFile(hPipe, szIn, sizeof(szIn), &cbRead, &os);
		if (!bRet && (GetLastError() == ERROR_IO_PENDING)) {
			dwWait = WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);
			if (dwWait != WAIT_OBJECT_0 + 1) break;
		}
		_stprintf(szOut, TEXT("ServiceKiller! [%s]"), szIn);
		memset(&os, 0, sizeof(OVERLAPPED));
		os.hEvent = hEvents[1];
		ResetEvent(hEvents[1]);
		bRet = WriteFile(hPipe, szOut, sizeof(szOut), &cbWritten, &os);
		if (!bRet && (GetLastError() == ERROR_IO_PENDING)) {
			dwWait = WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);
			if (dwWait != WAIT_OBJECT_0 + 1)break;
		}
		DisconnectNamedPipe(hPipe);
	}
cleanup:
	if (hPipe != INVALID_HANDLE_VALUE)    CloseHandle(hPipe);
	if (hServerStopEvent)      CloseHandle(hServerStopEvent);
	if (hEvents[1]) CloseHandle(hEvents[1]);
	if (pSD)free(pSD);
}
VOID ServiceStop() { if (hServerStopEvent)     SetEvent(hServerStopEvent); }

int APIENTRY KillMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR     lpCmdLine, int       nCmdShow) {
	InitCommonControls();
	hInst = hInstance; //formward l'instance
	memset(&wc, 0, sizeof(wc));
	wc.hCursor = LoadCursor(hInstance, 0);
	wc.lpfnWndProc = DefDlgProc;
	wc.cbWndExtra = DLGWINDOWEXTRA;
	wc.hInstance = hInstance;
	wc.lpszClassName = "ServiceKiller32";
	wc.hbrBackground = (HBRUSH)CreateSolidBrush(RGB(64, 64, 128));//couleur de fond
	wc.hIcon = LoadIcon(hInstance, (LPCTSTR)IDI_ICON1);
	wc.style = CS_VREDRAW | CS_HREDRAW | CS_SAVEBITS | CS_DBLCLKS;
	RegisterClass(&wc);
	//	if(!HyperLien_InitControl())        return 0;
	return DialogBox(hInstance, (LPCTSTR)IDD_FORMVIEW, NULL, (DLGPROC)DialogFunc);
}
static BOOL CALLBACK DialogKillFunc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
	hMain = hDlg;
	switch (msg) {
	case WM_INITDIALOG:
		hList = GetDlgItem(hDlg, IDC_LIST1);
		SendMessage(GetDlgItem(hDlg, IDCANCEL), WM_SETFONT, (WPARAM)GetStockObject(0x1E), MAKELPARAM(TRUE, 0)); //aplique la police au bouton designe
		SendMessage(GetDlgItem(hDlg, IDOK), WM_SETFONT, (WPARAM)GetStockObject(0x1F), MAKELPARAM(TRUE, 0));
		nf.cbSize = sizeof(nf);
		nf.hIcon = wc.hIcon;
		nf.hWnd = hDlg;
		strcpy(nf.szTip, "ServiceKiller32 ©1995 - 2015\0");
		nf.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		Shell_NotifyIcon(NIM_ADD, &nf);
		SendMessage(CreateWindowEx(0, "STATIC", NULL, WS_VISIBLE | WS_CHILD | SS_ICON, 10, 10, 10, 10, hDlg, (HMENU)45000, wc.hInstance, NULL), STM_SETIMAGE, IMAGE_ICON, (LPARAM)LoadIcon(wc.hInstance, (LPCTSTR)IDI_ICON1));
		sprintf(buffer, "Nous sommes %s, le %2d %s %4d", jours[st.wDayOfWeek], st.wDay, mois[st.wMonth - 1], st.wYear); //creation du string de date
		SetWindowText(hDlg, nf.szTip);
		CreateStatusWindow(WS_VISIBLE | WS_CHILD, nf.szTip, hDlg, 6000);
		SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)__argv[0]);

		//	enumservices();
		return TRUE;
	case WM_COMMAND:// instruction des commandes menu et boutons etc...
		switch (LOWORD(wParam)) {
		case 1000: {enumservices();
		}
				   break;
		case IDCANCEL:
			Shell_NotifyIcon(NIM_DELETE, &nf);
			PostQuitMessage(0);
			return 1;
		}
		break;
	case WM_DRAWITEM:
		lpdis = (LPDRAWITEMSTRUCT)lParam;
		SetTextColor(lpdis->hDC, RGB(200, 200, 200));
		SetBkColor(lpdis->hDC, RGB(0, 200, 200));
		return TRUE;
	case WM_TIMER:
		GetLocalTime(&st);
		sprintf(buffer, "Nous sommes %s, le %2d %s %4d %.2d:%.2d:%.2d", jours[st.wDayOfWeek], st.wDay, mois[st.wMonth - 1], st.wYear, st.wHour, st.wMinute, st.wSecond);
		SetDlgItemText(hDlg, 6000, buffer);
	case WM_CTLCOLORDLG:
		return (long)wc.hbrBackground;
		break;

	case WM_CTLCOLORSTATIC:
		if (LOWORD(wParam) == -1) {
			SetTextColor((HDC)wParam, RGB(0, 255, 255));
		}
		else {
			SetTextColor((HDC)wParam, RGB(255, 0, 255));
			SetBkMode((HDC)wParam, TRANSPARENT);
		}return (LONG)(HBRUSH)wc.hbrBackground;
	case WM_CTLCOLORLISTBOX:
		SetTextColor((HDC)wParam, RGB(0, 255, 0));
		SetBkMode((HDC)wParam, TRANSPARENT);
		return (LONG)(HBRUSH)wc.hbrBackground;
	case WM_CTLCOLOREDIT:
		SetTextColor((HDC)wParam, RGB(0, 255, 0));
		SetBkMode((HDC)wParam, TRANSPARENT);
		return (LONG)(HBRUSH)wc.hbrBackground;
	case WM_CLOSE:
		Shell_NotifyIcon(NIM_DELETE, &nf);
		PostQuitMessage(0);
		return TRUE;
	}
	return FALSE;
}
int enumservices() {
	char compname[MAX_COMPUTERNAME_LENGTH + 1];
	DWORD cbComputerName = 32;
	DWORD cbBufSize = 512 * 31;
	DWORD lpServicesReturned;
	DWORD pcbBytesNeeded;
	DWORD lpResumeHandle = 0;
	DWORD dwBytesNeeded;
	char szStatus[255];
	char szStartType[255];
	int i = 0;
	GetComputerName(compname, &cbComputerName);
	SetDlgItemText(hMain, IDC_EDIT1, compname);
	ScManager = OpenSCManager(compname, NULL, SC_MANAGER_ALL_ACCESS);
	if (ScManager == NULL) { SetDlgItemText(hMain, 6000, "Error querying the service manager"); return 0; }
	if (EnumServicesStatus(ScManager, SERVICE_WIN32, SERVICE_STATE_ALL, EnService, cbBufSize, &pcbBytesNeeded, &lpServicesReturned, &lpResumeHandle) == 0) { SetDlgItemText(hMain, 6000, "Error querying the service manager");		return 0; }
	for (i = 0; i < (INT)lpServicesReturned; i++) {
		if ((ScService = ::OpenService(ScManager, EnService[i].lpServiceName, SERVICE_ALL_ACCESS)) == NULL) { SetDlgItemText(hMain, 6000, "Error opening service"); }
		lpqscBuf = (LPQUERY_SERVICE_CONFIG)LocalAlloc(LPTR, 4096);
		if (lpqscBuf == NULL) { SetDlgItemText(hMain, 6000, "Error allocating service query"); sprintf(szStartType, "Unknown"); }
		if (!QueryServiceConfig(ScService, lpqscBuf, 4096, &dwBytesNeeded)) { SetDlgItemText(hMain, 6000, "Error querying service info"); sprintf(szStartType, "Unknown"); }
		switch (lpqscBuf->dwStartType) {
		case SERVICE_AUTO_START:sprintf(szStartType, "Automatic"); goto getstate;
		case SERVICE_DEMAND_START:sprintf(szStartType, "Manual"); goto getstate;
		case SERVICE_DISABLED:sprintf(szStartType, "Disabled"); goto getstate;
		}
	getstate:
		switch (EnService[i].ServiceStatus.dwCurrentState) {
		case SERVICE_PAUSED:sprintf(szStatus, "Paused"); goto output;
		case SERVICE_RUNNING:sprintf(szStatus, "Running"); goto output;
		case SERVICE_STOPPED:sprintf(szStatus, "Stopped"); goto output;
		case SERVICE_START_PENDING: sprintf(szStatus, "Start pending"); goto output;
		case SERVICE_STOP_PENDING: sprintf(szStatus, "Stop pending"); goto output;
		output:
			wsprintf(buffer, "Nom du service: %s\nNom d'affichage: %s\nStatut: %s\nType de démarrage: %s\n", EnService[i].lpServiceName, EnService[i].lpDisplayName, szStatus, szStartType);
			SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buffer);
		}
	}
	return 0;
}
int RemoveService(char* NomDuService) {
	WNDCLASS	wc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hbrBackground = (HBRUSH)CreateSolidBrush(RGB(64, 64, 64));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hInstance = hInst;
	wc.lpfnWndProc = 0;
	wc.lpszClassName = "XServices";
	wc.lpszMenuName = NULL;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	if (!RegisterClass(&wc)) {
		MessageBox(NULL, TEXT("Erreur lors de la création de la Classe XService/Liste"), TEXT("Erreur XService"), MB_OK);
		return 0;
		HWND hList = CreateWindow("LISTBOX", "Suppression de Service", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME || LBS_STANDARD | LBS_SORT | LBS_NOTIFY | WS_BORDER | WS_VSCROLL | WS_CAPTION | WS_VISIBLE, , 0, 0, 320, 240, 0, 0, hInst, 0);
		GetLocalTime(&st);
		char buf[255];
		SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)"Version: 7.0.015.213 © Patrice Wächter-Ebling 1995-2019");
		wsprintf(buf, "Il est %.2d:%.2d:%.2d Nous sommes %s, le %2d %.3s %4d", st.wHour, st.wMinute, st.wSecond, jours[st.wDayOfWeek], st.wDay, mois[st.wMonth - 1], st.wYear);
		SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buf);
		wsprintf(buf, "Nom du service à tuer : %s", NomDuService);
		SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buf);
		schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
		if (schSCManager) {
			schService = OpenService(schSCManager, NomDuService, DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);
			if (schService) {
				if (ControlService(schService, SERVICE_CONTROL_STOP, &ssStatus)) {
					wsprintf(buf, "Tentative d'arrêt du service: %s.", NomDuService);
					SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buf);
					Sleep(1000);
					while (QueryServiceStatus(schService, &ssStatus)) {
						if (ssStatus.dwCurrentState == SERVICE_STOP_PENDING) {
							SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)".");
							Sleep(1000);
						}
						else {
							break;
						}
						if (ssStatus.dwCurrentState == SERVICE_STOPPED) {
							wsprintf(buf, "%s a été arrtêté", NomDuService);
							SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buf);
						}
						else {
							wsprintf(buf, "%s n'a pas été arrtêté", NomDuService);
							SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buf);
						}
						if (DeleteService(schService)) {
							wsprintf(buf, "%s a été déinstallé", NomDuService);
							SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buf);
						}
						else {
							wsprintf(buf, "Echec lors de la déinsatallation de %s [%s]", NomDuService, GetLastErrorText(szErr, 256));
							SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buf);
						}
						CloseServiceHandle(schService);
					}
				}
				else {
					wsprintf(buf, "Echec à l'ouverture du Service [%s]", GetLastErrorText(szErr, 256));
					SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buf);
				}
				CloseServiceHandle(schSCManager);
			}
			else {
				wsprintf(buf, "Echec à l'ouverture du gestionnaire de Services [%s]", GetLastErrorText(szErr, 256));
				SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buf);
			}
			return 0;
		}
	}
	void CServicesScannerDlg() {
		sc = ::OpenSCManager(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
		if (sc != NULL) {
			ENUM_SERVICE_STATUS service_data, *lpservice;
			BOOL retVal;
			DWORD bytesNeeded, srvCount, resumeHandle = 0, srvType, srvState;
			srvType = SERVICE_WIN32;
			srvState = SERVICE_STATE_ALL;
			retVal = ::EnumServicesStatus(sc, srvType, srvState, &service_data, sizeof(service_data),
				&bytesNeeded, &srvCount, &resumeHandle);
			DWORD err = GetLastError();
			if ((retVal == FALSE) || err == ERROR_MORE_DATA) {
				DWORD dwBytes = bytesNeeded + sizeof(ENUM_SERVICE_STATUS);
				lpservice = new ENUM_SERVICE_STATUS[dwBytes];
				EnumServicesStatus(sc, srvType, srvState, lpservice, dwBytes, &bytesNeeded, &srvCount, &resumeHandle);
			}
			char buffer[256];
			progress.SetRange(0, (short)srvCount);
			progress.SetBkColor(RGB(128, 128, 128));
			for (int i = 0; i < srvCount; i++) {
				wsprintf(buffer, "\"%s\" \"%s\"", lpservice[i].lpServiceName, lpservice[i].lpDisplayName);
				progress.SetPos(i);
				m_scan.AddString(buffer);
				SetDlgItemInt(IDC_STATUT, i, 0);
			}
		}
		CloseServiceHandle(sc);
	}