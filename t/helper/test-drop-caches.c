#include "git-compat-util.h"

#if defined(GIT_WINDOWS_NATIVE)

int cmd_sync(void)
{
	char Buffer[MAX_PATH];
	DWORD dwRet;
	char szVolumeAccessPath[] = "\\\\.\\X:";
	HANDLE hVolWrite;
	int success = 0;

	dwRet = GetCurrentDirectory(MAX_PATH, Buffer);
	if ((0 == dwRet) || (dwRet > MAX_PATH))
		return error("Error getting current directory");

	if ((Buffer[0] < 'A') || (Buffer[0] > 'Z'))
		return error("Invalid drive letter '%c'", Buffer[0]);

	szVolumeAccessPath[4] = Buffer[0];
	hVolWrite = CreateFile(szVolumeAccessPath, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (INVALID_HANDLE_VALUE == hVolWrite)
		return error("Unable to open volume for writing, need admin access");

	success = FlushFileBuffers(hVolWrite);
	if (!success)
		error("Unable to flush volume");

	CloseHandle(hVolWrite);

	return !success;
}

#define STATUS_SUCCESS			(0x00000000L)
#define STATUS_PRIVILEGE_NOT_HELD	(0xC0000061L)

typedef enum _SYSTEM_INFORMATION_CLASS {
	SystemMemoryListInformation = 80,
} SYSTEM_INFORMATION_CLASS;

typedef enum _SYSTEM_MEMORY_LIST_COMMAND {
	MemoryCaptureAccessedBits,
	MemoryCaptureAndResetAccessedBits,
	MemoryEmptyWorkingSets,
	MemoryFlushModifiedList,
	MemoryPurgeStandbyList,
	MemoryPurgeLowPriorityStandbyList,
	MemoryCommandMax
} SYSTEM_MEMORY_LIST_COMMAND;

BOOL GetPrivilege(HANDLE TokenHandle, LPCSTR lpName, int flags)
{
	BOOL bResult;
	DWORD dwBufferLength;
	LUID luid;
	TOKEN_PRIVILEGES tpPreviousState;
	TOKEN_PRIVILEGES tpNewState;

	dwBufferLength = 16;
	bResult = LookupPrivilegeValueA(0, lpName, &luid);
	if (bResult) {
		tpNewState.PrivilegeCount = 1;
		tpNewState.Privileges[0].Luid = luid;
		tpNewState.Privileges[0].Attributes = 0;
		bResult = AdjustTokenPrivileges(TokenHandle, 0, &tpNewState, (DWORD)((LPBYTE)&(tpNewState.Privileges[1]) - (LPBYTE)&tpNewState), &tpPreviousState, &dwBufferLength);
		if (bResult) {
			tpPreviousState.PrivilegeCount = 1;
			tpPreviousState.Privileges[0].Luid = luid;
			tpPreviousState.Privileges[0].Attributes = flags != 0 ? 2 : 0;
			bResult = AdjustTokenPrivileges(TokenHandle, 0, &tpPreviousState, dwBufferLength, 0, 0);
		}
	}
	return bResult;
}

int cmd_dropcaches(void)
{
	HANDLE hProcess = GetCurrentProcess();
	HANDLE hToken;
	int status;

	if (!OpenProcessToken(hProcess, TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken))
		return error("Can't open current process token");

	if (!GetPrivilege(hToken, "SeProfileSingleProcessPrivilege", 1))
		return error("Can't get SeProfileSingleProcessPrivilege");

	CloseHandle(hToken);

	HMODULE ntdll = LoadLibrary("ntdll.dll");
	if (!ntdll)
		return error("Can't load ntdll.dll, wrong Windows version?");

	DWORD(WINAPI *NtSetSystemInformation)(INT, PVOID, ULONG) =
		(DWORD(WINAPI *)(INT, PVOID, ULONG))GetProcAddress(ntdll, "NtSetSystemInformation");
	if (!NtSetSystemInformation)
		return error("Can't get function addresses, wrong Windows version?");

	SYSTEM_MEMORY_LIST_COMMAND command = MemoryPurgeStandbyList;
	status = NtSetSystemInformation(
		SystemMemoryListInformation,
		&command,
		sizeof(SYSTEM_MEMORY_LIST_COMMAND)
	);
	if (status == STATUS_PRIVILEGE_NOT_HELD)
		error("Insufficient privileges to purge the standby list, need admin access");
	else if (status != STATUS_SUCCESS)
		error("Unable to execute the memory list command %d", status);

	FreeLibrary(ntdll);

	return status;
}

#elif defined(__linux__)

int cmd_sync(void)
{
	return system("sync");
}

int cmd_dropcaches(void)
{
	return system("echo 3 | sudo tee /proc/sys/vm/drop_caches");
}

#elif defined(__APPLE__)

int cmd_sync(void)
{
	return system("sync");
}

int cmd_dropcaches(void)
{
	return system("sudo purge");
}

#else

int cmd_sync(void)
{
	return 0;
}

int cmd_dropcaches(void)
{
	return error("drop caches not implemented on this platform");
}

#endif

int cmd_main(int argc, const char **argv)
{
	cmd_sync();
	return cmd_dropcaches();
}
