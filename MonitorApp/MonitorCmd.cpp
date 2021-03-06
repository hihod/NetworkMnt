#include "windows.h"
#include "winioctl.h"
#include "strsafe.h"
#include "process.h"

#ifndef _CTYPE_DISABLE_MACROS
#define _CTYPE_DISABLE_MACROS
#endif

#include "fwpmu.h"

#include "winsock2.h"
#include "ws2def.h"

#include <stdlib.h>
#include <conio.h>
#include <stdio.h>

#include "Common.h"
#include "Format.h"

#include "InstallService.h"

#define INITGUID
#include <guiddef.h>
#include "MntGuid.h"


#define MONITOR_FLOW_ESTABLISHED_CALLOUT_DESCRIPTION L"SP Network Monitor - Flow Established Callout"
#define MONITOR_FLOW_ESTABLISHED_CALLOUT_NAME L"The Flow Established Callout"

#define MONITOR_STREAM_CALLOUT_DESCRIPTION L"SP Network Monitor - Stream Callout"
#define MONITOR_STREAM_CALLOUT_NAME L"The Stream Callout"


BOOL gRotateMode;
UINT64 gSentBytes = 0;
UINT64 gReceivedBytes = 0;
UINT64 tSentBytes = 0;
UINT64 tReceivedBytes = 0;

MONITOR_SETTINGS gMonitorSettings;
BOOL gStopMntThread;

HANDLE gMntDataThreadHandle;
UINT gUMDThreadID = (UINT)-1;
CRITICAL_SECTION gCSMdThrad;
const DWORD dwWaitInterval = 3000; //ms

static UINT WINAPI ThreadGetMntData(PVOID param)
{
	UNREFERENCED_PARAMETER(param);
	//HANDLE hDevice = (HANDLE)param;

	DWORD dwResult = 0;
	static int nFlag = 0;
	while (!gStopMntThread)
	{
		dwResult = WaitForMultipleObjects(DEFAULT_EVENT_NUM, gMonitorSettings.hInforEvents, FALSE, INFINITE);
		switch (dwResult)
		{
		case WAIT_OBJECT_0: //Process network traffic create.
		{
			//printf("test for gMonitorSettings.hInforEvents[0] %d\n", ++nFlag);

		}
		break;
		case WAIT_OBJECT_0 + 1: // Process close.
		{
			//printf("test for gMonitorSettings.hInforEvents[1]\n");
		}
		break;
		case WAIT_OBJECT_0 + 2: // quit.
		{
			//printf("test for gMonitorSettings.hInforEvents[2]\n");
			nFlag = 0;
			gStopMntThread = TRUE;
		}
		break;
		default:
			printf("Unexpect WaitForMultipleObjects result %d.\n", dwResult);
			break;
		}
	}
	return 0;
}

DWORD InitSettings(HANDLE hDevice)
{
	//Init monitior settings
	if (gUMDThreadID != (UINT)-1)
		return (DWORD)-1;

	RtlZeroMemory(&gMonitorSettings, sizeof(MONITOR_SETTINGS));

	gMonitorSettings.monitorOperation = monitorTraffic;
	for (int idx = 0; idx < DEFAULT_EVENT_NUM; idx++)
	{
		gMonitorSettings.hInforEvents[idx] = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (gMonitorSettings.hInforEvents[idx] == NULL)
		{
			printf("CreateEvent %d failed 0x%08x\n", idx, GetLastError());
			return 1;
		}
	}
	gStopMntThread = FALSE;
	gMntDataThreadHandle = NULL;
	InitializeCriticalSection(&gCSMdThrad);

	gMntDataThreadHandle = (HANDLE)_beginthreadex(NULL, 0, ThreadGetMntData, &hDevice, 0, &gUMDThreadID);
	if (gMntDataThreadHandle == NULL || gMntDataThreadHandle == INVALID_HANDLE_VALUE)
	{
		DeleteCriticalSection(&gCSMdThrad);
		gStopMntThread = TRUE;
		gUMDThreadID = (UINT)-1;
		for (int idx = 0; idx < DEFAULT_EVENT_NUM; idx++)
		{
			CloseHandle(gMonitorSettings.hInforEvents[idx]);
			gMonitorSettings.hInforEvents[idx] = NULL;
		}
		printf("CreateThread for Receiving event failed 0x%08x\n", GetLastError());
		return 2;
	}
	return 0;
}

BOOL WINAPI CustomCtrlHandle(DWORD dwCtrlType)
{
	switch (dwCtrlType)
	{
	case CTRL_C_EVENT:
		gRotateMode = FALSE;
		return TRUE;

	case CTRL_BREAK_EVENT:
		printf("Total sent %I64d bytes, received %I64d bytes, total %I64d bytes before Reset\n", 
			gSentBytes, gReceivedBytes, gSentBytes + gReceivedBytes);
		gSentBytes = gReceivedBytes = 0;
		return TRUE;

	case CTRL_CLOSE_EVENT:

		return FALSE;

	case CTRL_SHUTDOWN_EVENT:


		return FALSE;

	default:
		return FALSE;
	}
}

DWORD GetAppIdFromPath(_In_ PCWSTR fileName, _Out_ FWP_BYTE_BLOB** appId)
{
	DWORD result = NO_ERROR;
	result = FwpmGetAppIdFromFileName(fileName, appId);
	return result;
}

DWORD GetInformation(_In_ HANDLE monitorDevice, _Out_ MONITOR_INFORMATION* Info)
{
	DWORD result = NO_ERROR;
	DWORD bytesReturned;


	//if (!DeviceIoControl(monitorDevice, MONITOR_IOCTL_GETINFO_MONITOR, NULL, 0, Info, 
	//	sizeof(MONITOR_INFORMATION), &bytesReturned, NULL))
	//{
	//	return GetLastError();
	//}

	if (!ReadFile(monitorDevice, Info, sizeof(MONITOR_INFORMATION), &bytesReturned, NULL))
	{
		return GetLastError();
	}

	printf("bytesReturned %d ", bytesReturned);
	return result;
}

DWORD GetInformations(_In_ HANDLE monitorDevice, _Out_ PMONITOR_INFORMATIONS Infos)
{
	DWORD result = NO_ERROR;
	DWORD bytesReturned;


	if (!DeviceIoControl(monitorDevice, MONITOR_IOCTL_GETINFOS_MONITOR, NULL, 0, Infos, 
		sizeof(MONITOR_INFORMATIONS), &bytesReturned, NULL))
	{
		return GetLastError();
	}
	//printf("bytesReturned %d ", bytesReturned);
	return result;
}


VOID ShowInformations(_In_ PMONITOR_INFORMATIONS infors)
{
	UINT32 numInformations = 0;
	MONITOR_INFORMATION curInfor;
	char buffer[64] = { 0 };

	if (infors == NULL)
	{
		return;
	}



	if (infors->numMonitorInformations <= 0)
	{
		printf("There no information about process network activities\n");
	}
	else
	{
		tSentBytes = 0;
		tReceivedBytes = 0;
		printf("   PID  Download    Upload  TotalDownload  TotalUpload  ProcessName\n");
		for (; numInformations < infors->numMonitorInformations; numInformations++)
		{
			curInfor = infors->monitorInformation[numInformations];
			memset(buffer, 0, sizeof(buffer));
			ByteSprintf(buffer, 16, (double)curInfor.receivedBytes, TRUE);
			ByteSprintf(buffer + 16, 16, (double)curInfor.sentBytes, TRUE);
			ByteSprintf(buffer + 32, 16, (double)curInfor.totalRecvBytes, FALSE);
			ByteSprintf(buffer + 48, 16, (double)curInfor.totalSetnBytes, FALSE);

			printf("%6I64d  %s  %s         %s       %s  %ws \n", curInfor.processId, buffer, buffer + 16, buffer + 32, buffer + 48, curInfor.processPath);

			//printf("%I64d\t %I64d\t %I64d\t %I64d\t %I64d\t %ws \n", curInfor.processId, curInfor.receivedBytes,
			//	curInfor.sentBytes, curInfor.totalRecvBytes, curInfor.totalSetnBytes, curInfor.processPath);
			gSentBytes += curInfor.sentBytes;
			gReceivedBytes += curInfor.receivedBytes;
			tSentBytes += curInfor.sentBytes;
			tReceivedBytes += curInfor.receivedBytes;
		}
		printf("=======================================================================\n");
		memset(buffer, 0, sizeof(buffer));
		ByteSprintf(buffer, 16, (double)tReceivedBytes, TRUE);
		ByteSprintf(buffer + 16, 16, (double)tSentBytes, TRUE);
		ByteSprintf(buffer + 32, 16, (double)gReceivedBytes, FALSE);
		ByteSprintf(buffer + 48, 16, (double)gSentBytes, FALSE);
		printf("下载速度：%s   上传速度：%s   总下载流量：%s   总上传流量：%s\n", buffer, buffer + 16, buffer + 32, buffer + 48);
	}
	return;
}

DWORD EnableMonitoring(_In_ HANDLE monitorDevice, _In_ MONITOR_SETTINGS* monitorSettings)
{
	DWORD bytesReturned;

	if (!DeviceIoControl(monitorDevice, MONITOR_IOCTL_ENABLE_MONITOR, monitorSettings,
		sizeof(MONITOR_SETTINGS), NULL, 0, &bytesReturned, NULL))
	{
		return GetLastError();
	}
	return NO_ERROR;
}

DWORD DisableMonitoring(_In_ HANDLE monitorDevice)
{
	DWORD bytesReturned;

	if (!DeviceIoControl(monitorDevice, MONITOR_IOCTL_DISABLE_MONITOR, NULL, 0,
		NULL, 0, &bytesReturned, NULL))
	{
		return GetLastError();
	}
	return NO_ERROR;
}

//warming: applicationPath is applicationId
DWORD AddFilters(_In_ HANDLE engineHandle, _In_ FWP_BYTE_BLOB* applicationPath)
{
	DWORD result = NO_ERROR;
	FWPM_SUBLAYER monitorSubLayer;
	FWPM_FILTER filter;
	FWPM_FILTER_CONDITION filterConditions[1];
	UNREFERENCED_PARAMETER(applicationPath);
	BOOL inTransaction = FALSE;
	RtlZeroMemory(&monitorSubLayer, sizeof(FWPM_SUBLAYER));

	monitorSubLayer.subLayerKey = NETWORK_MONITOR_SUBLAYER;
	monitorSubLayer.displayData.name = L"Network Monitor Sub layer";
	monitorSubLayer.displayData.description = L"Network Monitor Sub layer";
	monitorSubLayer.flags = 0;
	// We don't really mind what the order of invocation is.
	monitorSubLayer.weight = 0;

	//printf("Starting Transaction for adding filters\n");
	result = FwpmTransactionBegin(engineHandle, 0);
	if (NO_ERROR != result)
	{
		printf("Start Transaction failed in AddFilters routine with status 0x%08x\n", result);
		goto Cleanup;
	}
	inTransaction = TRUE;
	printf("Successfully Started Transaction in AddFilters routine\n");

	//Null means to let system assigns a default security descriptor
	result = FwpmSubLayerAdd(engineHandle, &monitorSubLayer, NULL);
	if (NO_ERROR != result)
	{
		printf("Add SubLayer failed with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Sucessfully added Sublayer\n");
	
	RtlZeroMemory(&filter, sizeof(FWPM_FILTER));
	filter.layerKey = FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4;
	filter.displayData.name = L"Flow Established Filter.";
	filter.displayData.description = L"Sets up flow for traffic that we are interested in.";
	filter.action.type = FWP_ACTION_CALLOUT_INSPECTION; // We're only doing inspection.
	filter.action.calloutKey =	NETWORK_MONITOR_FLOW_ESTABLISHED_CALLOUT_V4;
	filter.filterCondition = filterConditions;
	///// Add sublayer key to a filter.
	filter.subLayerKey = monitorSubLayer.subLayerKey;
	///
	filter.weight.type = FWP_EMPTY;

	filter.numFilterConditions = 1;
	RtlZeroMemory(filterConditions, sizeof(filterConditions));
	//filterConditions[0].fieldKey = FWPM_CONDITION_ALE_APP_ID;
	//filterConditions[0].matchType = FWP_MATCH_EQUAL;
	//filterConditions[0].conditionValue.type = FWP_BYTE_BLOB_TYPE;
	//filterConditions[0].conditionValue.byteBlob = applicationPath;

	filterConditions[0].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
	filterConditions[0].matchType = FWP_MATCH_EQUAL;
	filterConditions[0].conditionValue.type = FWP_UINT8;
	filterConditions[0].conditionValue.uint8 = IPPROTO_TCP;

	//printf("Adding Flow Established Filter\n");
	result = FwpmFilterAdd(engineHandle, &filter, NULL, NULL);
	if (NO_ERROR != result)
	{
		printf("Add Flow Established Filter failed with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully added Flow Established filter\n");

	RtlZeroMemory(&filter, sizeof(FWPM_FILTER));
	//This filtering layer is located in the stream data path for inspecting any stream data that has been discarded
	filter.layerKey = FWPM_LAYER_STREAM_V4;
	filter.displayData.name = L"Stream Layer Filter";
	filter.displayData.description = L"Monitors TCP traffic.";
	filter.action.type = FWP_ACTION_CALLOUT_INSPECTION;
	filter.action.calloutKey = NETWORK_MONITOR_STREAM_CALLOUT_V4;
	filter.subLayerKey = monitorSubLayer.subLayerKey;
	filter.weight.type = FWP_EMPTY; // auto-weight.
	filter.filterCondition = filterConditions;
	filter.numFilterConditions = 0;
	RtlZeroMemory(filterConditions, sizeof(filterConditions));

	//printf("Adding Stream Filter\n");
	result = FwpmFilterAdd(engineHandle, &filter, NULL, NULL);
	if (NO_ERROR != result)
	{
		printf("Add Stream Filter failed with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully added Stream filter\n");

	//printf("Committing Transaction\n");
	result = FwpmTransactionCommit(engineHandle);
	if (NO_ERROR != result)
	{
		printf("Committed Transaction failed in AddFilters routine with status 0x%08x.\n", result);
		goto Cleanup;
	}
	printf("Successfully committed Transaction in AddFilters\n");
	inTransaction = FALSE;
Cleanup:
	if (NO_ERROR != result && inTransaction)
	{
		printf("Aborting Transaction\n");
		result = FwpmTransactionAbort(engineHandle);
		if (NO_ERROR != result)
		{
			printf("Aborted Transaction failed in AddFilters routine with status 0x%08x.\n", result);
		}
	}
	return result;
}


//不增加过滤条件
DWORD AddFiltersWithNoCondiction(_In_ HANDLE engineHandle)
{
	DWORD result = NO_ERROR;
	FWPM_SUBLAYER monitorSubLayer;
	FWPM_FILTER filter;
	FWPM_FILTER_CONDITION filterConditions[1];
	BOOL inTransaction = FALSE;
	RtlZeroMemory(&monitorSubLayer, sizeof(FWPM_SUBLAYER));

	monitorSubLayer.subLayerKey = NETWORK_MONITOR_SUBLAYER;
	monitorSubLayer.displayData.name = L"Network Monitor Sub layer";
	monitorSubLayer.displayData.description = L"Network Monitor Sub layer";
	monitorSubLayer.flags = 0;
	// We don't really mind what the order of invocation is.
	monitorSubLayer.weight = 0;

	//printf("Starting Transaction for adding filters\n");
	result = FwpmTransactionBegin(engineHandle, 0);
	if (NO_ERROR != result)
	{
		printf("Start Transaction failed in AddFilters routine with status 0x%08x\n", result);
		goto Cleanup;
	}
	inTransaction = TRUE;
	printf("Successfully Started Transaction in AddFilters routine\n");

	//Null means to let system assigns a default security descriptor
	result = FwpmSubLayerAdd(engineHandle, &monitorSubLayer, NULL);
	if (NO_ERROR != result)
	{
		printf("Add SubLayer failed with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Sucessfully added Sublayer\n");

	RtlZeroMemory(&filter, sizeof(FWPM_FILTER));
	filter.layerKey = FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4;
	filter.displayData.name = L"Flow Established Filter.";
	filter.displayData.description = L"Sets up flow for traffic that we are interested in.";
	filter.action.type = FWP_ACTION_CALLOUT_INSPECTION; // We're only doing inspection.
	filter.action.calloutKey = NETWORK_MONITOR_FLOW_ESTABLISHED_CALLOUT_V4;
	filter.filterCondition = filterConditions;
	///// Add sublayer key to a filter.
	filter.subLayerKey = monitorSubLayer.subLayerKey;
	///
	filter.weight.type = FWP_EMPTY;

	filter.numFilterConditions = 1;
	RtlZeroMemory(filterConditions, sizeof(filterConditions));
	filterConditions[0].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
	filterConditions[0].matchType = FWP_MATCH_EQUAL;
	filterConditions[0].conditionValue.type = FWP_UINT8;
	filterConditions[0].conditionValue.uint8 = IPPROTO_TCP;

	//printf("Adding Flow Established Filter\n");
	result = FwpmFilterAdd(engineHandle, &filter, NULL, NULL);
	if (NO_ERROR != result)
	{
		printf("Add Flow Established Filter failed with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully added Flow Established filter\n");

	RtlZeroMemory(&filter, sizeof(FWPM_FILTER));
	//This filtering layer is located in the stream data path for inspecting any stream data that has been discarded
	filter.layerKey = FWPM_LAYER_STREAM_V4;
	filter.displayData.name = L"Stream Layer Filter";
	filter.displayData.description = L"Monitors TCP traffic.";
	filter.action.type = FWP_ACTION_CALLOUT_INSPECTION;
	filter.action.calloutKey = NETWORK_MONITOR_STREAM_CALLOUT_V4;
	filter.subLayerKey = monitorSubLayer.subLayerKey;
	filter.weight.type = FWP_EMPTY; // auto-weight.
	filter.filterCondition = filterConditions;
	filter.numFilterConditions = 0;
	RtlZeroMemory(filterConditions, sizeof(filterConditions));

	//printf("Adding Stream Filter\n");
	result = FwpmFilterAdd(engineHandle, &filter, NULL, NULL);
	if (NO_ERROR != result)
	{
		printf("Add Stream Filter failed with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully added Stream filter\n");

	//printf("Committing Transaction\n");
	result = FwpmTransactionCommit(engineHandle);
	if (NO_ERROR != result)
	{
		printf("Committed Transaction failed in AddFilters routine with status 0x%08x.\n", result);
		goto Cleanup;
	}
	printf("Successfully committed Transaction in AddFilters\n");
	inTransaction = FALSE;
Cleanup:
	if (NO_ERROR != result && inTransaction)
	{
		printf("Aborting Transaction\n");
		result = FwpmTransactionAbort(engineHandle);
		if (NO_ERROR != result)
		{
			printf("Aborted Transaction failed in AddFilters routine with status 0x%08x.\n", result);
		}
	}
	return result;
}

DWORD AddCallouts()
{
	FWPM_CALLOUT callout;
	DWORD result;
	FWPM_DISPLAY_DATA displayData;
	HANDLE engineHandle = NULL;
	FWPM_SESSION session;
	RtlZeroMemory(&session, sizeof(FWPM_SESSION));
	BOOL inTransaction = FALSE;

	session.displayData.name = L"Network Monitor Non-Dynamic Session";
	session.displayData.description = L"For Adding callouts";

	result = FwpmEngineOpen(NULL, RPC_C_AUTHN_WINNT, NULL, &session, &engineHandle);
	if (NO_ERROR != result)
	{
		printf("Open Filter Engine failed in AddCallouts routine with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully opened Filter Engine in AddCallouts routine\n");

	//printf("Starting Transaction for adding callouts\n");
	result = FwpmTransactionBegin(engineHandle, 0);

	if (NO_ERROR != result)
	{
		printf("Start Transaction failed in AddCallouts routine with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully started the Transaction in AddCallouts routine\n");
	inTransaction = TRUE;

	RtlZeroMemory(&callout, sizeof(FWPM_CALLOUT));
	displayData.description = MONITOR_FLOW_ESTABLISHED_CALLOUT_DESCRIPTION;
	displayData.name = MONITOR_FLOW_ESTABLISHED_CALLOUT_NAME;
	callout.calloutKey = NETWORK_MONITOR_FLOW_ESTABLISHED_CALLOUT_V4;
	callout.displayData = displayData;
	callout.applicableLayer = FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4;
	//Persistent objects live until they are deleted
	callout.flags = FWPM_CALLOUT_FLAG_PERSISTENT; // Make this a persistent callout.

	result = FwpmCalloutAdd(engineHandle, &callout, NULL, NULL);
	if (NO_ERROR != result)
	{
		printf("Add Established Callout Object failed with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully Added Persistent Flow Established Callout Object.\n");

	RtlZeroMemory(&callout, sizeof(FWPM_CALLOUT));
	displayData.description = MONITOR_STREAM_CALLOUT_DESCRIPTION;
	displayData.name = MONITOR_STREAM_CALLOUT_DESCRIPTION;
	callout.calloutKey = NETWORK_MONITOR_STREAM_CALLOUT_V4;
	callout.displayData = displayData;
	callout.applicableLayer = FWPM_LAYER_STREAM_V4;
	callout.flags = FWPM_CALLOUT_FLAG_PERSISTENT; // Make this a persistent callout.
	result = FwpmCalloutAdd(engineHandle, &callout, NULL, NULL);
	if (NO_ERROR != result)
	{
		printf("Add Stream Callout Object failed with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully Added Persistent Stream Callout Object.\n");

	result = FwpmTransactionCommit(engineHandle);
	if (NO_ERROR != result)
	{
		printf("Committed Transaction failed with status 0x%08x.\n", result);
		goto Cleanup;
	}
	printf("Successfully Committed Transaction in AddCallouts routine.\n");
	inTransaction = FALSE;

Cleanup:
	if (NO_ERROR != result)
	{
		if (inTransaction)
		{
			printf("Aborting Transaction in AddCallouts routine.\n");
			result = FwpmTransactionAbort(engineHandle);
			if (NO_ERROR != result)
			{
				printf("Aborted Transaction failed in AddCallouts routine with status 0x%08x.\n", result);
			}
		}
	}

	if (engineHandle)
	{
		result = FwpmEngineClose(engineHandle);
		if (NO_ERROR == result)
		{
			printf("\n");
		}
		else
		{
			printf("Cloese Filter Engine failed in AddCallouts routine with status 0x%08x.\n", result);
		}
	}
	return result;
}

DWORD RemoveCallouts()
{
	DWORD result;
	HANDLE engineHandle = NULL;
	FWPM_SESSION session;
	RtlZeroMemory(&session, sizeof(FWPM_SESSION));
	BOOL inTransaction = FALSE;
	session.displayData.name = L"Network Monitor Non-Dynamic Session";
	session.displayData.description = L"For Adding callouts";

	result = FwpmEngineOpen(NULL, RPC_C_AUTHN_WINNT, NULL, &session, &engineHandle);
	if (NO_ERROR != result)
	{
		printf("Open Filter Engine failed in RemoveCallouts routine with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully open Filter Engine in RemoveCallouts routine\n");

	result = FwpmTransactionBegin(engineHandle, 0);
	if (NO_ERROR != result)
	{
		printf("Start Transaction failed in RemoveCallouts routine with status 0x%08x\n", result);
		goto Cleanup;
	}
	inTransaction = TRUE;
	printf("Successfully started the Transaction in RemoveCallouts routine\n");
	
	result = FwpmCalloutDeleteByKey(engineHandle, &NETWORK_MONITOR_FLOW_ESTABLISHED_CALLOUT_V4);
	if (NO_ERROR != result)
	{
		printf("Remove Established Callout failed in RemoveCallouts routine with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully Remove Established callout in RemoveCallouts routine\n");

	result = FwpmCalloutDeleteByKey(engineHandle, &NETWORK_MONITOR_STREAM_CALLOUT_V4);
	if (NO_ERROR != result)
	{
		printf("Remove Stream Callout failed in RemoveCallouts routine with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully Remove Stream callout in RemoveCallouts routine\n");


	result = FwpmTransactionCommit(engineHandle);
	if (NO_ERROR != result)
	{
		printf("Committed Transaction failed in RemoveCallouts routine with status 0x%08x.\n", result);
		goto Cleanup;
	}
	printf("Successfully committed Transaction in RemoveCallouts routine\n");
	inTransaction = FALSE;

Cleanup:
	if (NO_ERROR != result)
	{
		if (inTransaction)
		{
			printf("Aborting Transaction\n");
			result = FwpmTransactionAbort(engineHandle);
			if (NO_ERROR != result)
			{
				printf("Aborted Transaction failed in RemoveCallouts routine with status 0x%08x\n", result);
			}
		}
	}
	if (engineHandle)
	{
		result = FwpmEngineClose(engineHandle);
		if (NO_ERROR == result)
		{
			printf("\n");
		}
		else
		{
			printf("Cloese Filter Engine failed in RemoveCallouts routine with status 0x%08x.\n", result);
		}
	}
	return result;
}

DWORD OpenMonitorDevice(_Out_ HANDLE* monitorDevice)
{
	DWORD errCode = NO_ERROR;
	if (!monitorDevice)
	{
		return ERROR_HV_INVALID_PARAMETER;
	}
	*monitorDevice = CreateFileW(MONITOR_DOS_NAME, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	
	if (*monitorDevice == INVALID_HANDLE_VALUE)
	{
		errCode = GetLastError();
		if (errCode == ERROR_FILE_NOT_FOUND)
		{
			printf("Please run MonitorService.exe before run current process...\n");
		}
	}
	return errCode;
}

BOOL CloseMonitorDevice(_In_ HANDLE monitorDevice)
{
	return CloseHandle(monitorDevice);
}

DWORD OperateGoodMonitoring(PCWSTR AppPath)
{
	HANDLE monitorDevice = NULL;
	HANDLE engineHandle = NULL;
	DWORD result;
	MONITOR_SETTINGS monitorSettings;
	MONITOR_INFORMATION monitorInfor;
	FWPM_SESSION session;
	FWP_BYTE_BLOB* applicationId = NULL;

	gSentBytes = 0;
	gReceivedBytes = 0;

	gRotateMode = TRUE;

	RtlZeroMemory(&monitorSettings, sizeof(MONITOR_SETTINGS));
	RtlZeroMemory(&monitorInfor, sizeof(MONITOR_INFORMATION));
	RtlZeroMemory(&session, sizeof(FWPM_SESSION));

	session.displayData.name = L"Network Monitor Session";
	session.displayData.description = L"Monitors traffic at the Stream layer.";

	session.flags = FWPM_SESSION_FLAG_DYNAMIC;

	result = FwpmEngineOpen(NULL, RPC_C_AUTHN_WINNT, NULL, &session, &engineHandle);
	if (NO_ERROR != result)
	{
		printf("Open Filter Engine failed in Monitoring with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully opened Filter Engine\n");

	result = GetAppIdFromPath(AppPath, &applicationId);
	if (NO_ERROR != result)
	{
		printf("Retrieved Destination Application's Id failed with status 0x%08x\n", result);
		goto Cleanup;
	}

	printf("Opening Network Monitor Device\n");
	result = OpenMonitorDevice(&monitorDevice);
	if (NO_ERROR != result)
	{
		printf("Open Monitor Device failed with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully opened Monitor Device\n");

	printf("Adding Filters through the Filter Engine\n");
	result = AddFilters(engineHandle, applicationId);
	if (NO_ERROR != result)
	{
		printf("Adding Filters failed with status 0x%08x\n", result);
		goto Cleanup;
	}

	printf("Enabling Monitoring\n");
	result = EnableMonitoring(monitorDevice, &monitorSettings);
	if (NO_ERROR != result)
	{
		printf("Enabled Monitoring failed with status 0x%08x\n", result);
		goto Cleanup;
	}

	printf("Successfully enabled monitoring.\n");

	while (gRotateMode)
	{
		result = GetInformation(monitorDevice, &monitorInfor);
		if (NO_ERROR != result)
		{
			printf("GetInformation failed with status 0x%08x", result);
			break;
		}
		printf("sent %I64d bytes, received %I64d bytes, processId %I64d\n",
			monitorInfor.sentBytes, monitorInfor.receivedBytes, monitorInfor.processId);
		gReceivedBytes += monitorInfor.receivedBytes;
		gSentBytes += monitorInfor.sentBytes;
		Sleep(1000);
	}

//	printf("Events will be traced through WMI. Please press any key to exit and cleanup filters.\n");
//#pragma prefast(push)
//#pragma prefast(disable:6031, "by design the return value of _getch() is ignored here")
//	_getch();
//#pragma prefast(pop)
	printf("Total sent %I64d bytes, received %I64d bytes, total %I64d bytes from the last time\n", 
		gSentBytes, gReceivedBytes, gSentBytes + gReceivedBytes);

Cleanup:

	if (monitorDevice)
	{
		DisableMonitoring(monitorDevice);
		CloseMonitorDevice(monitorDevice);
	}

	if (applicationId)
	{
		FwpmFreeMemory((void**)&applicationId);
	}

	if (engineHandle)
	{
		result = FwpmEngineClose(engineHandle);
	}
	return result;
}

DWORD OperateMonitoring()
{
	HANDLE monitorDevice = NULL;
	HANDLE engineHandle = NULL;
	DWORD result;

	MONITOR_INFORMATION monitorInfor;
	MONITOR_INFORMATIONS monitorInfors;
	FWPM_SESSION session;
	
	gSentBytes = 0;
	gReceivedBytes = 0;

	gRotateMode = TRUE;

	RtlZeroMemory(&monitorInfor, sizeof(MONITOR_INFORMATION));
	RtlZeroMemory(&session, sizeof(FWPM_SESSION));

	session.displayData.name = L"Network Monitor Session";
	session.displayData.description = L"Monitors traffic at the Stream layer.";

	session.flags = FWPM_SESSION_FLAG_DYNAMIC;

	result = FwpmEngineOpen(NULL, RPC_C_AUTHN_WINNT, NULL, &session, &engineHandle);
	if (NO_ERROR != result)
	{
		printf("Open Filter Engine failed in Monitoring with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully opened Filter Engine\n");

	printf("Opening Network Monitor Device\n");
	result = OpenMonitorDevice(&monitorDevice);
	if (NO_ERROR != result)
	{
		printf("Open Monitor Device failed with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully opened Monitor Device\n");

	printf("Adding Filters through the Filter Engine\n");
	result = AddFiltersWithNoCondiction(engineHandle);
	if (NO_ERROR != result)
	{
		printf("Adding Filters failed with status 0x%08x\n", result);
		goto Cleanup;
	}

	printf("Enabling Monitoring\n");

	InitSettings(monitorDevice);

	result = EnableMonitoring(monitorDevice, &gMonitorSettings/*monitorSettings*/);
	if (NO_ERROR != result)
	{
		printf("Enabled Monitoring failed with status 0x%08x\n", result);
		goto Cleanup;
	}

	printf("Successfully enabled monitoring.\n");

#if 0

	while (gRotateMode)
	{
		result = GetInformation(monitorDevice, &monitorInfor);
		if (NO_ERROR != result)
		{
			printf("GetInformation failed with status 0x%08x", result);
			break;
		}
		printf("sent %I64d bytes, received %I64d bytes\n",monitorInfor.sentBytes, monitorInfor.receivedBytes);
		gReceivedBytes += monitorInfor.receivedBytes;
		gSentBytes += monitorInfor.sentBytes;
		Sleep(1000);
	}

#else

	while (gRotateMode)
	{
		result = GetInformations(monitorDevice, &monitorInfors);
		if (NO_ERROR != result)
		{
			printf("GetInformation failed with status 0x%08x\n", result);
			break;
		}
		ShowInformations(&monitorInfors);
		Sleep(1000);
		system("cls");
	}

#endif

	printf("Total sent %I64d bytes, received %I64d bytes, total %I64d bytes from the last time\n",
		gSentBytes, gReceivedBytes, gSentBytes + gReceivedBytes);

Cleanup:

	if (monitorDevice)
	{
		DisableMonitoring(monitorDevice);
		CloseMonitorDevice(monitorDevice);
	}

	if (engineHandle)
	{
		result = FwpmEngineClose(engineHandle);
	}

	if (gUMDThreadID != -1 && !gStopMntThread)
	{
		printf("Waiting thread done...");
		SetEvent(gMonitorSettings.hInforEvents[DEFAULT_EVENT_NUM - 1]);
		WaitForSingleObject(gMntDataThreadHandle, dwWaitInterval);
		gUMDThreadID = (UINT)-1;
		printf("Thread is done.");
	}

	return result;
}

DWORD OperateMonitoringEvent()
{
	HANDLE monitorDevice = NULL;
	HANDLE engineHandle = NULL;
	DWORD result;
	DWORD byteReturned;
	BOOL bStatus;
	
	MONITOR_INFORMATION monitorInfor;
	FWPM_SESSION session;

	REGISTER_EVENT registerEvent;

	RtlZeroMemory(&gMonitorSettings, sizeof(MONITOR_SETTINGS));
	RtlZeroMemory(&monitorInfor, sizeof(MONITOR_INFORMATION));
	RtlZeroMemory(&session, sizeof(FWPM_SESSION));

	session.displayData.name = L"Network Monitor Session";
	session.displayData.description = L"Monitors traffic at the Stream layer.";

	session.flags = FWPM_SESSION_FLAG_DYNAMIC;

	result = FwpmEngineOpen(NULL, RPC_C_AUTHN_WINNT, NULL, &session, &engineHandle);
	if (NO_ERROR != result)
	{
		printf("Open Filter Engine failed in Monitoring with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully opened Filter Engine\n");

	printf("Opening Network Monitor Device\n");

	//Init monitior settings
	gMonitorSettings.monitorOperation = monitorTraffic;
	for (int idx = 0; idx < DEFAULT_EVENT_NUM; idx++)
	{
		gMonitorSettings.hInforEvents[idx] = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (gMonitorSettings.hInforEvents[idx] == NULL)
		{
			printf("CreateEvent %d failed 0x%08x\n", idx, GetLastError());
			goto Cleanup;
		}
	}
	gStopMntThread = FALSE;
	_beginthreadex(NULL, 0, ThreadGetMntData, NULL, 0, NULL);


	result = OpenMonitorDevice(&monitorDevice);
	if (NO_ERROR != result)
	{
		printf("Open Monitor Device failed with status 0x%08x\n", result);
		goto Cleanup;
	}
	printf("Successfully opened Monitor Device\n");

	printf("Adding Filters through the Filter Engine\n");
	result = AddFiltersWithNoCondiction(engineHandle);
	if (NO_ERROR != result)
	{
		printf("Adding Filters failed with status 0x%08x\n", result);
		goto Cleanup;
	}

	printf("Enabling Monitoring\n");
	result = EnableMonitoring(monitorDevice, &gMonitorSettings);
	if (NO_ERROR != result)
	{
		printf("Enabled Monitoring failed with status 0x%08x\n", result);
		goto Cleanup;
	}

	printf("Successfully enabled monitoring.\n");
	//
	// set the event signal delay. Use relative time for this sample
	//
	registerEvent.hEvent = CreateEvent(NULL, TRUE, FALSE, "MonitorEvent");
	if (!registerEvent.hEvent)
	{
		printf("CreateEvent failed with status %d\n", GetLastError());
		goto Cleanup;
	}
	registerEvent.DueTime.QuadPart = -((LONGLONG)(5 * 10.0E6));
	printf("Event HANDLE = %p\n", registerEvent.hEvent);
	printf("Press any key to exit.\n");
	while (!_kbhit())
	{
		bStatus = DeviceIoControl(monitorDevice, MONITOR_IOCTL_REGISTER_EVENT, &registerEvent, SIZEOF_REGISTER_EVENT, NULL, 0, &byteReturned, NULL);
		if (!bStatus)
		{
			printf("Send IOCTL failed with status %d\n", GetLastError());
			break;
		}
		else
		{
			printf("Waiting for Event...\n");
			//printf("Event occurred.\n\n");
			//printf("\nRegistering event again....\n\n");
			WaitForSingleObject(registerEvent.hEvent, INFINITE);
			printf("Event signalled.\n\n");
			ResetEvent(registerEvent.hEvent);
		}
	}

Cleanup:

	if (monitorDevice)
	{
		DisableMonitoring(monitorDevice);
		CloseMonitorDevice(monitorDevice);
	}

	if (engineHandle)
	{
		result = FwpmEngineClose(engineHandle);
	}
	return result;
}

int __cdecl wmain(_In_ int argc, _In_reads_(argc) PCWSTR argv[])
{
	DWORD result = NO_ERROR;
	BOOL bIntialEnv;
	UNREFERENCED_PARAMETER(argc);
	UNREFERENCED_PARAMETER(argv);

	if ((bIntialEnv = SetConsoleCtrlHandler((PHANDLER_ROUTINE)CustomCtrlHandle, TRUE)) == FALSE)
	{
		wprintf(L"Warning: SetConsoleCtrlHandler failed.\n");
	}

	result = CustomOpenService(DRIVER_NAME);

	if (result != NO_ERROR && result != ERROR_SERVICE_ALREADY_RUNNING)
	{
		goto Cleanup;
	}
	if (AddCallouts() != NO_ERROR)
	{
		goto Cleanup;
	}

	result = OperateMonitoring();

	RemoveCallouts();

	result = CustomCloseService(DRIVER_NAME);
	
#if 0

	if (argc == 1)
	{
		result = CustomOpenService(DRIVER_NAME);

		if (result != NO_ERROR && result != ERROR_SERVICE_ALREADY_RUNNING)
		{
			goto Cleanup;
		}
		if (AddCallouts() != NO_ERROR)
		{
			goto Cleanup;
		}

		result = OperateMonitoring();
		
		RemoveCallouts();

		result = CustomCloseService(DRIVER_NAME);

		goto Cleanup;
	}

	if (argc == 2)
	{
		if (_wcsicmp(argv[1], L"addcallouts") == 0)
		{
			result = AddCallouts();
			goto Cleanup;
		}
		if (_wcsicmp(argv[1], L"delcallouts") == 0)
		{
			result = RemoveCallouts();
			goto Cleanup;
		}
		if (_wcsicmp(argv[1], L"monitor") == 0)
		{
			AddCallouts();
			result = OperateMonitoring();
			//result = OperateMonitoringEvent();
			RemoveCallouts();
			goto Cleanup;
		}
		if (_wcsicmp(argv[1], L"install") == 0)
		{
			result = InstallDriver(DRIVER_NAME);
			goto Cleanup;
		}

		if (_wcsicmp(argv[1], L"start") == 0)
		{
			result = CustomOpenService(DRIVER_NAME);
			goto Cleanup;
		}

		if (_wcsicmp(argv[1], L"stop") == 0)
		{
			result = CustomCloseService(DRIVER_NAME);
			goto Cleanup;
		}
		if (_wcsicmp(argv[1], L"unload") == 0)
		{
			result = UnloadDriver(DRIVER_NAME);
			goto Cleanup;
		}
	}
	if (argc == 3)
	{
		if (_wcsicmp(argv[1], L"install") == 0 && _wcsicmp(argv[2], L"-force") == 0)
		{
			
			if (FileExistedOrNot(MONITOR_DOS_NAME) == NO_ERROR)
			{
				result = CustomCloseService(DRIVER_NAME);
				if (result != NO_ERROR)
				{
					goto Cleanup;
				}
				result = UnloadDriver(DRIVER_NAME);
				if (result != NO_ERROR)
				{
					goto Cleanup;
				}
			}
			result = InstallDriver(DRIVER_NAME);
			if (result == ERROR_SERVICE_EXISTS)
			{
				result = UnloadDriver(DRIVER_NAME);
				if (result != NO_ERROR)
				{
					goto Cleanup;
				}
				result = InstallDriver(DRIVER_NAME);
			}
			goto Cleanup;
		}

		if (_wcsicmp(argv[1], L"unload") == 0 && _wcsicmp(argv[2], L"-force") == 0)
		{
			result = UnloadDriver(DRIVER_NAME);
			if (result == ERROR_SERVICE_MARKED_FOR_DELETE)
			{
				result = CustomCloseService(DRIVER_NAME);
			}
			goto Cleanup;
		}

		if (_wcsicmp(argv[1], L"monitor") == 0)
		{
			AddCallouts();
			result = OperateGoodMonitoring(argv[2]);
			RemoveCallouts();
			goto Cleanup;
		}
	}
		
	wprintf(L"Usage: MonitorApp (install <-force> | start | stop | unload <-force> | addcallouts | delcallouts )\n");

#endif

Cleanup:

	if (bIntialEnv)
	{
		SetConsoleCtrlHandler((PHANDLER_ROUTINE)CustomCtrlHandle, FALSE);
	}
	return (int)result;
}