// m_sys_net.c : implementation related to the Sys/Net built-in module.
//
// The 'sys/net' module is responsible for displaying networking information
// in a 'netstat' like way at the path '/sys/net/'
//
// The module is a provider of forensic timelining information.
//
// (c) Ulf Frisk, 2020-2021
// Author: Ulf Frisk, pcileech@frizk.net
//
#include "vmm.h"
#include "util.h"
#include "fc.h"

LPCSTR szMSYSNET_README =
"Information about the sys net module                                         \n" \
"====================================                                         \n" \
"The sys/net module tries to enumerate and list active TCP connections in     \n" \
"Windows 7 and later (x64 only).  It currently does not support listening TCP \n" \
"ports or UDP ports. This functionality is planned for the future. Also, it's \n" \
"not supporting 32-bit or Windows Vista/XP (future support less likely).      \n" \
"For more information please visit: https://github.com/ufrisk/MemProcFS/wiki  \n";

// ----------------------------------------------------------------------------
// Net functionality below:
// Show information related to TCP/IP connectivity in the analyzed system.
// ----------------------------------------------------------------------------

#define MSYSNET_LINELENGTH                  128ULL
#define MSYSNET_LINELENGTH_VERBOSE          278ULL
#define MSYSNET_LINEHEADER                  L"   #    PID Proto  State        Src                           Dst                          Process"
#define MSYSNET_LINEHEADER_VERBOSE          MSYSNET_LINEHEADER L"              Time                     Object Address    Process Path"



VOID MSysNet_ReadLine_Callback(_Inout_opt_ PVOID ctx, _In_ DWORD cbLineLength, _In_ DWORD ie, _In_ PVMM_MAP_NETENTRY pe, _Out_writes_(cbLineLength + 1) LPSTR szu8)
{
    PVMM_PROCESS pObProcess = VmmProcessGet(pe->dwPID);
    Util_snwprintf_u8ln(szu8, cbLineLength,
        L"%04x%7i %s %S",
        ie,
        pe->dwPID,
        pe->wszText,
        pObProcess ? pObProcess->pObPersistent->uszNameLong : ""
    );
    Ob_DECREF(pObProcess);
}

VOID MSysNet_ReadLineVerbose_Callback(_Inout_opt_ PVOID ctx, _In_ DWORD cbLineLength, _In_ DWORD ie, _In_ PVMM_MAP_NETENTRY pe, _Out_writes_(cbLineLength + 1) LPSTR szu8)
{
    CHAR szTime[24];
    PVMM_PROCESS pObProcess = VmmProcessGet(pe->dwPID);
    Util_FileTime2String(pe->ftTime, szTime);
    Util_snwprintf_u8ln(szu8, cbLineLength,
        L"%04x%7i %s %-20S %S  %016llx  %S",
        ie,
        pe->dwPID,
        pe->wszText,
        pObProcess ? pObProcess->pObPersistent->uszNameLong : "",
        szTime,
        pe->vaObj,
        pObProcess ? pObProcess->pObPersistent->uszPathKernel : ""
    );
    Ob_DECREF(pObProcess);
}

NTSTATUS MSysNet_Read(_In_ PVMMDLL_PLUGIN_CONTEXT ctx, _Out_writes_to_(cb, *pcbRead) PBYTE pb, _In_ DWORD cb, _Out_ PDWORD pcbRead, _In_ QWORD cbOffset)
{
    NTSTATUS nt = VMMDLL_STATUS_FILE_INVALID;
    PVMMOB_MAP_NET pObNetMap;
    if(!wcscmp(ctx->wszPath, L"readme.txt")) {
        return Util_VfsReadFile_FromPBYTE((PBYTE)szMSYSNET_README, strlen(szMSYSNET_README), pb, cb, pcbRead, cbOffset);
    }
    if(VmmMap_GetNet(&pObNetMap)) {
        if(!wcscmp(ctx->wszPath, L"netstat.txt")) {
            nt = Util_VfsLineFixed_Read(
                MSysNet_ReadLine_Callback, NULL, MSYSNET_LINELENGTH, MSYSNET_LINEHEADER,
                pObNetMap->pMap, pObNetMap->cMap, sizeof(VMM_MAP_NETENTRY),
                pb, cb, pcbRead, cbOffset
            );
        }
        if(!wcscmp(ctx->wszPath, L"netstat-v.txt")) {
            nt = Util_VfsLineFixed_Read(
                MSysNet_ReadLineVerbose_Callback, NULL, MSYSNET_LINELENGTH_VERBOSE, MSYSNET_LINEHEADER_VERBOSE,
                pObNetMap->pMap, pObNetMap->cMap, sizeof(VMM_MAP_NETENTRY),
                pb, cb, pcbRead, cbOffset
            );
        }
        Ob_DECREF(pObNetMap);
    }
    return nt;
}

BOOL MSysNet_List(_In_ PVMMDLL_PLUGIN_CONTEXT ctx, _Inout_ PHANDLE pFileList)
{
    PVMMOB_MAP_NET pObNetMap;
    if(ctx->wszPath[0]) { return FALSE; }
    VMMDLL_VfsList_AddFile(pFileList, L"readme.txt", strlen(szMSYSNET_README), NULL);
    if(VmmMap_GetNet(&pObNetMap)) {
        VMMDLL_VfsList_AddFile(pFileList, L"netstat.txt", UTIL_VFSLINEFIXED_LINECOUNT(pObNetMap->cMap) * MSYSNET_LINELENGTH, NULL);
        VMMDLL_VfsList_AddFile(pFileList, L"netstat-v.txt", UTIL_VFSLINEFIXED_LINECOUNT(pObNetMap->cMap) * MSYSNET_LINELENGTH_VERBOSE, NULL);
        Ob_DECREF(pObNetMap);
    }
    return TRUE;
}

VOID MSysNet_Timeline(
    _In_opt_ PVOID ctxfc,
    _In_ HANDLE hTimeline,
    _In_ VOID(*pfnAddEntry)(_In_ HANDLE hTimeline, _In_ QWORD ft, _In_ DWORD dwAction, _In_ DWORD dwPID, _In_ QWORD qwValue, _In_ LPWSTR wszText),
    _In_ VOID(*pfnEntryAddBySql)(_In_ HANDLE hTimeline, _In_ DWORD cEntrySql, _In_ LPSTR *pszEntrySql)
) {
    DWORD i;
    PVMM_MAP_NETENTRY pe;
    PVMMOB_MAP_NET pObNetMap;
    if(VmmMap_GetNet(&pObNetMap)) {
        for(i = 0; i < pObNetMap->cMap; i++) {
            pe = pObNetMap->pMap + i;
            if(pe->ftTime && pe->wszText[0]) {
                pfnAddEntry(hTimeline, pe->ftTime, FC_TIMELINE_ACTION_CREATE, pe->dwPID, pe->vaObj, pe->wszText);
            }
        }
        Ob_DECREF(pObNetMap);
    }
}

VOID M_SysNet_Initialize(_Inout_ PVMMDLL_PLUGIN_REGINFO pRI)
{
    if((pRI->magic != VMMDLL_PLUGIN_REGINFO_MAGIC) || (pRI->wVersion != VMMDLL_PLUGIN_REGINFO_VERSION)) { return; }
    if((pRI->tpSystem != VMM_SYSTEM_WINDOWS_X64) && (pRI->tpSystem != VMM_SYSTEM_WINDOWS_X86)) { return; }
    wcscpy_s(pRI->reg_info.wszPathName, 128, L"\\sys\\net");    // module name
    pRI->reg_info.fRootModule = TRUE;                           // module shows in root directory
    pRI->reg_fn.pfnList = MSysNet_List;                         // List function supported
    pRI->reg_fn.pfnRead = MSysNet_Read;                         // Read function supported
    pRI->reg_fnfc.pfnTimeline = MSysNet_Timeline;               // Timeline supported
    memcpy(pRI->reg_info.sTimelineNameShort, "Net   ", 6);
    strncpy_s(pRI->reg_info.szTimelineFileUTF8, 32, "timeline_net.txt", _TRUNCATE);
    strncpy_s(pRI->reg_info.szTimelineFileJSON, 32, "timeline_net.json", _TRUNCATE);
    pRI->pfnPluginManager_Register(pRI);
}
