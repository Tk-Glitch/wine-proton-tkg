/* Unit test suite for *Information* Registry API functions
 *
 * Copyright 2005 Paul Vriens
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

#include "ntdll_test.h"
#include <winnls.h>
#include <stdio.h>

static NTSTATUS (WINAPI * pNtQuerySystemInformation)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
static NTSTATUS (WINAPI * pNtSetSystemInformation)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG);
static NTSTATUS (WINAPI * pRtlGetNativeSystemInformation)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
static NTSTATUS (WINAPI * pNtQuerySystemInformationEx)(SYSTEM_INFORMATION_CLASS, void*, ULONG, void*, ULONG, ULONG*);
static NTSTATUS (WINAPI * pNtPowerInformation)(POWER_INFORMATION_LEVEL, PVOID, ULONG, PVOID, ULONG);
static NTSTATUS (WINAPI * pNtQueryInformationProcess)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
static NTSTATUS (WINAPI * pNtQueryInformationThread)(HANDLE, THREADINFOCLASS, PVOID, ULONG, PULONG);
static NTSTATUS (WINAPI * pNtSetInformationProcess)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG);
static NTSTATUS (WINAPI * pNtSetInformationThread)(HANDLE, THREADINFOCLASS, PVOID, ULONG);
static NTSTATUS (WINAPI * pNtReadVirtualMemory)(HANDLE, const void*, void*, SIZE_T, SIZE_T*);
static NTSTATUS (WINAPI * pNtQueryVirtualMemory)(HANDLE, LPCVOID, MEMORY_INFORMATION_CLASS , PVOID , SIZE_T , SIZE_T *);
static NTSTATUS (WINAPI * pNtCreateSection)(HANDLE*,ACCESS_MASK,const OBJECT_ATTRIBUTES*,const LARGE_INTEGER*,ULONG,ULONG,HANDLE);
static NTSTATUS (WINAPI * pNtMapViewOfSection)(HANDLE,HANDLE,PVOID*,ULONG_PTR,SIZE_T,const LARGE_INTEGER*,SIZE_T*,SECTION_INHERIT,ULONG,ULONG);
static NTSTATUS (WINAPI * pNtUnmapViewOfSection)(HANDLE,PVOID);
static NTSTATUS (WINAPI * pNtClose)(HANDLE);
static ULONG    (WINAPI * pNtGetCurrentProcessorNumber)(void);
static BOOL     (WINAPI * pIsWow64Process)(HANDLE, PBOOL);
static BOOL     (WINAPI * pGetLogicalProcessorInformationEx)(LOGICAL_PROCESSOR_RELATIONSHIP,SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*,DWORD*);
static DEP_SYSTEM_POLICY_TYPE (WINAPI * pGetSystemDEPPolicy)(void);
static NTSTATUS (WINAPI * pNtOpenThread)(HANDLE *, ACCESS_MASK, const OBJECT_ATTRIBUTES *, const CLIENT_ID *);
static NTSTATUS (WINAPI * pNtQueryObject)(HANDLE, OBJECT_INFORMATION_CLASS, void *, ULONG, ULONG *);
static NTSTATUS (WINAPI * pNtCreateDebugObject)( HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *, ULONG );
static NTSTATUS (WINAPI * pNtSetInformationDebugObject)(HANDLE,DEBUGOBJECTINFOCLASS,PVOID,ULONG,ULONG*);
static NTSTATUS (WINAPI * pDbgUiConvertStateChangeStructure)(DBGUI_WAIT_STATE_CHANGE*,DEBUG_EVENT*);

static BOOL is_wow64;

/* one_before_last_pid is used to be able to compare values of a still running process
   with the output of the test_query_process_times and test_query_process_handlecount tests.
*/
static DWORD one_before_last_pid = 0;

static inline DWORD_PTR get_affinity_mask(DWORD num_cpus)
{
    if (num_cpus >= sizeof(DWORD_PTR) * 8) return ~(DWORD_PTR)0;
    return ((DWORD_PTR)1 << num_cpus) - 1;
}

#define NTDLL_GET_PROC(func) do {                     \
    p ## func = (void*)GetProcAddress(hntdll, #func); \
    if(!p ## func) { \
      trace("GetProcAddress(%s) failed\n", #func); \
      return FALSE; \
    } \
  } while(0)

/* Firmware table providers */
#define ACPI 0x41435049
#define FIRM 0x4649524D
#define RSMB 0x52534D42

static BOOL InitFunctionPtrs(void)
{
    /* All needed functions are NT based, so using GetModuleHandle is a good check */
    HMODULE hntdll = GetModuleHandleA("ntdll");
    HMODULE hkernel32 = GetModuleHandleA("kernel32");

    NTDLL_GET_PROC(NtQuerySystemInformation);
    NTDLL_GET_PROC(NtSetSystemInformation);
    NTDLL_GET_PROC(RtlGetNativeSystemInformation);
    NTDLL_GET_PROC(NtPowerInformation);
    NTDLL_GET_PROC(NtQueryInformationProcess);
    NTDLL_GET_PROC(NtQueryInformationThread);
    NTDLL_GET_PROC(NtSetInformationProcess);
    NTDLL_GET_PROC(NtSetInformationThread);
    NTDLL_GET_PROC(NtReadVirtualMemory);
    NTDLL_GET_PROC(NtQueryVirtualMemory);
    NTDLL_GET_PROC(NtClose);
    NTDLL_GET_PROC(NtCreateSection);
    NTDLL_GET_PROC(NtMapViewOfSection);
    NTDLL_GET_PROC(NtUnmapViewOfSection);
    NTDLL_GET_PROC(NtOpenThread);
    NTDLL_GET_PROC(NtQueryObject);
    NTDLL_GET_PROC(NtCreateDebugObject);
    NTDLL_GET_PROC(NtSetInformationDebugObject);
    NTDLL_GET_PROC(DbgUiConvertStateChangeStructure);

    /* not present before XP */
    pNtGetCurrentProcessorNumber = (void *) GetProcAddress(hntdll, "NtGetCurrentProcessorNumber");

    pIsWow64Process = (void *)GetProcAddress(hkernel32, "IsWow64Process");
    if (!pIsWow64Process || !pIsWow64Process( GetCurrentProcess(), &is_wow64 )) is_wow64 = FALSE;

    pGetSystemDEPPolicy = (void *)GetProcAddress(hkernel32, "GetSystemDEPPolicy");

    /* starting with Win7 */
    pNtQuerySystemInformationEx = (void *) GetProcAddress(hntdll, "NtQuerySystemInformationEx");
    if (!pNtQuerySystemInformationEx)
        win_skip("NtQuerySystemInformationEx() is not supported, some tests will be skipped.\n");

    pGetLogicalProcessorInformationEx = (void *) GetProcAddress(hkernel32, "GetLogicalProcessorInformationEx");

    return TRUE;
}

static void test_query_basic(void)
{
    NTSTATUS status;
    ULONG ReturnLength;
    SYSTEM_BASIC_INFORMATION sbi, sbi2;

    /* This test also covers some basic parameter testing that should be the same for 
     * every information class
    */

    /* Use a nonexistent info class */
    status = pNtQuerySystemInformation(-1, NULL, 0, NULL);
    ok( status == STATUS_INVALID_INFO_CLASS || status == STATUS_NOT_IMPLEMENTED /* vista */,
        "Expected STATUS_INVALID_INFO_CLASS or STATUS_NOT_IMPLEMENTED, got %08x\n", status);

    /* Use an existing class but with a zero-length buffer */
    status = pNtQuerySystemInformation(SystemBasicInformation, NULL, 0, NULL);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    /* Use an existing class, correct length but no SystemInformation buffer */
    status = pNtQuerySystemInformation(SystemBasicInformation, NULL, sizeof(sbi), NULL);
    ok( status == STATUS_ACCESS_VIOLATION || status == STATUS_INVALID_PARAMETER /* vista */,
        "Expected STATUS_ACCESS_VIOLATION or STATUS_INVALID_PARAMETER, got %08x\n", status);

    /* Use an existing class, correct length, a pointer to a buffer but no ReturnLength pointer */
    status = pNtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof(sbi), NULL);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);

    /* Check a too large buffer size */
    status = pNtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof(sbi) * 2, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    /* Finally some correct calls */
    status = pNtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof(sbi), &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( sizeof(sbi) == ReturnLength, "Inconsistent length %d\n", ReturnLength);

    /* Check if we have some return values */
    if (winetest_debug > 1) trace("Number of Processors : %d\n", sbi.NumberOfProcessors);
    ok( sbi.NumberOfProcessors > 0, "Expected more than 0 processors, got %d\n", sbi.NumberOfProcessors);

    memset(&sbi2, 0, sizeof(sbi2));
    status = pRtlGetNativeSystemInformation(SystemBasicInformation, &sbi2, sizeof(sbi2), &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x.\n", status);
    ok( sizeof(sbi2) == ReturnLength, "Unexpected length %u.\n", ReturnLength);

    ok( sbi.unknown == sbi2.unknown, "Expected unknown %#x, got %#x.\n", sbi.unknown, sbi2.unknown);
    ok( sbi.KeMaximumIncrement == sbi2.KeMaximumIncrement, "Expected KeMaximumIncrement %u, got %u.\n",
            sbi.KeMaximumIncrement, sbi2.KeMaximumIncrement);
    ok( sbi.PageSize == sbi2.PageSize, "Expected PageSize field %u, %u.\n", sbi.PageSize, sbi2.PageSize);
    ok( sbi.MmNumberOfPhysicalPages == sbi2.MmNumberOfPhysicalPages,
            "Expected MmNumberOfPhysicalPages %u, got %u.\n",
            sbi.MmNumberOfPhysicalPages, sbi2.MmNumberOfPhysicalPages);
    ok( sbi.MmLowestPhysicalPage == sbi2.MmLowestPhysicalPage, "Expected MmLowestPhysicalPage %u, got %u.\n",
            sbi.MmLowestPhysicalPage, sbi2.MmLowestPhysicalPage);
    ok( sbi.MmHighestPhysicalPage == sbi2.MmHighestPhysicalPage, "Expected MmHighestPhysicalPage %u, got %u.\n",
            sbi.MmHighestPhysicalPage, sbi2.MmHighestPhysicalPage);
    /* Higher 32 bits of AllocationGranularity is sometimes garbage on Windows. */
    ok( (ULONG)sbi.AllocationGranularity == (ULONG)sbi2.AllocationGranularity,
            "Expected AllocationGranularity %#lx, got %#lx.\n",
            sbi.AllocationGranularity, sbi2.AllocationGranularity);
    ok( sbi.LowestUserAddress == sbi2.LowestUserAddress, "Expected LowestUserAddress %p, got %p.\n",
            (void *)sbi.LowestUserAddress, (void *)sbi2.LowestUserAddress);
    /* Not testing HighestUserAddress. The field is different from NtQuerySystemInformation result
     * on 32 bit Windows (some of Win8 versions are the exception). Whenever it is different,
     * NtQuerySystemInformation returns user space limit (0x0x7ffeffff) and RtlGetNativeSystemInformation
     * returns address space limit (0xfffeffff). */
    ok( sbi.ActiveProcessorsAffinityMask == sbi2.ActiveProcessorsAffinityMask,
            "Expected ActiveProcessorsAffinityMask %#lx, got %#lx.\n",
            sbi.ActiveProcessorsAffinityMask, sbi2.ActiveProcessorsAffinityMask);
    ok( sbi.NumberOfProcessors == sbi2.NumberOfProcessors, "Expected NumberOfProcessors %u, got %u.\n",
            sbi.NumberOfProcessors, sbi2.NumberOfProcessors);
}

static void test_query_cpu(void)
{
    DWORD status;
    ULONG ReturnLength;
    SYSTEM_CPU_INFORMATION sci;

    status = pNtQuerySystemInformation(SystemCpuInformation, &sci, sizeof(sci), &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( sizeof(sci) == ReturnLength, "Inconsistent length %d\n", ReturnLength);

    /* Check if we have some return values */
    if (winetest_debug > 1) trace("Processor FeatureSet : %08x\n", sci.FeatureSet);
    ok( sci.FeatureSet != 0, "Expected some features for this processor, got %08x\n", sci.FeatureSet);
}

static void test_query_performance(void)
{
    NTSTATUS status;
    ULONG ReturnLength;
    ULONGLONG buffer[sizeof(SYSTEM_PERFORMANCE_INFORMATION)/sizeof(ULONGLONG) + 5];
    DWORD size = sizeof(SYSTEM_PERFORMANCE_INFORMATION);

    status = pNtQuerySystemInformation(SystemPerformanceInformation, buffer, 0, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    status = pNtQuerySystemInformation(SystemPerformanceInformation, buffer, size, &ReturnLength);
    if (status == STATUS_INFO_LENGTH_MISMATCH && is_wow64)
    {
        /* size is larger on wow64 under w2k8/win7 */
        size += 16;
        status = pNtQuerySystemInformation(SystemPerformanceInformation, buffer, size, &ReturnLength);
    }
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( ReturnLength == size, "Inconsistent length %d\n", ReturnLength);

    status = pNtQuerySystemInformation(SystemPerformanceInformation, buffer, size + 2, &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( ReturnLength == size || ReturnLength == size + 2 /* win8+ */,
        "Inconsistent length %d\n", ReturnLength);

    /* Not return values yet, as struct members are unknown */
}

static void test_query_timeofday(void)
{
    NTSTATUS status;
    ULONG ReturnLength;

    /* Copy of our winternl.h structure turned into a private one */
    typedef struct _SYSTEM_TIMEOFDAY_INFORMATION_PRIVATE {
        LARGE_INTEGER liKeBootTime;
        LARGE_INTEGER liKeSystemTime;
        LARGE_INTEGER liExpTimeZoneBias;
        ULONG uCurrentTimeZoneId;
        DWORD dwUnknown1[5];
    } SYSTEM_TIMEOFDAY_INFORMATION_PRIVATE;

    SYSTEM_TIMEOFDAY_INFORMATION_PRIVATE sti;
  
    status = pNtQuerySystemInformation( SystemTimeOfDayInformation, &sti, 0, &ReturnLength );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( 0 == ReturnLength, "ReturnLength should be 0, it is (%d)\n", ReturnLength);

    sti.uCurrentTimeZoneId = 0xdeadbeef;
    status = pNtQuerySystemInformation( SystemTimeOfDayInformation, &sti, 24, &ReturnLength );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( 24 == ReturnLength, "ReturnLength should be 24, it is (%d)\n", ReturnLength);
    ok( 0xdeadbeef == sti.uCurrentTimeZoneId, "This part of the buffer should not have been filled\n");

    sti.uCurrentTimeZoneId = 0xdeadbeef;
    status = pNtQuerySystemInformation( SystemTimeOfDayInformation, &sti, 32, &ReturnLength );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( 32 == ReturnLength, "ReturnLength should be 32, it is (%d)\n", ReturnLength);
    ok( 0xdeadbeef != sti.uCurrentTimeZoneId, "Buffer should have been partially filled\n");

    status = pNtQuerySystemInformation( SystemTimeOfDayInformation, &sti, 49, &ReturnLength );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);
    ok( ReturnLength == 0 || ReturnLength == sizeof(sti) /* vista */,
        "ReturnLength should be 0, it is (%d)\n", ReturnLength);

    status = pNtQuerySystemInformation( SystemTimeOfDayInformation, &sti, sizeof(sti), &ReturnLength );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( sizeof(sti) == ReturnLength, "Inconsistent length %d\n", ReturnLength);

    /* Check if we have some return values */
    if (winetest_debug > 1) trace("uCurrentTimeZoneId : (%d)\n", sti.uCurrentTimeZoneId);
}

static void test_query_process(void)
{
    NTSTATUS status;
    DWORD last_pid;
    ULONG ReturnLength;
    int i = 0, k = 0;
    SYSTEM_BASIC_INFORMATION sbi;
    PROCESS_BASIC_INFORMATION pbi;
    THREAD_BASIC_INFORMATION tbi;
    OBJECT_ATTRIBUTES attr;
    CLIENT_ID cid;
    HANDLE handle;

    /* Copy of our winternl.h structure turned into a private one */
    typedef struct _SYSTEM_PROCESS_INFORMATION_PRIVATE {
        ULONG NextEntryOffset;
        DWORD dwThreadCount;
        DWORD dwUnknown1[6];
        FILETIME ftCreationTime;
        FILETIME ftUserTime;
        FILETIME ftKernelTime;
        UNICODE_STRING ProcessName;
        DWORD dwBasePriority;
        HANDLE UniqueProcessId;
        HANDLE ParentProcessId;
        ULONG HandleCount;
        DWORD dwUnknown3;
        DWORD dwUnknown4;
        VM_COUNTERS_EX vmCounters;
        IO_COUNTERS ioCounters;
        SYSTEM_THREAD_INFORMATION ti[1];
    } SYSTEM_PROCESS_INFORMATION_PRIVATE;

    ULONG SystemInformationLength = sizeof(SYSTEM_PROCESS_INFORMATION_PRIVATE);
    SYSTEM_PROCESS_INFORMATION_PRIVATE *spi, *spi_buf = HeapAlloc(GetProcessHeap(), 0, SystemInformationLength);

    /* test ReturnLength */
    ReturnLength = 0;
    status = pNtQuerySystemInformation(SystemProcessInformation, NULL, 0, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH got %08x\n", status);
    ok( ReturnLength > 0, "got 0 length\n");

    /* W2K3 and later returns the needed length, the rest returns 0, so we have to loop */
    for (;;)
    {
        status = pNtQuerySystemInformation(SystemProcessInformation, spi_buf, SystemInformationLength, &ReturnLength);

        if (status != STATUS_INFO_LENGTH_MISMATCH) break;
        
        spi_buf = HeapReAlloc(GetProcessHeap(), 0, spi_buf , SystemInformationLength *= 2);
    }
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    spi = spi_buf;

    pNtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof(sbi), &ReturnLength);

    for (;;)
    {
        DWORD_PTR tid;
        DWORD j;

        i++;

        last_pid = (DWORD_PTR)spi->UniqueProcessId;
        ok(!(last_pid & 3), "Unexpected PID low bits: %p\n", spi->UniqueProcessId);
        for (j = 0; j < spi->dwThreadCount; j++)
        {
            k++;
            ok ( spi->ti[j].ClientId.UniqueProcess == spi->UniqueProcessId,
                 "The owning pid of the thread (%p) doesn't equal the pid (%p) of the process\n",
                 spi->ti[j].ClientId.UniqueProcess, spi->UniqueProcessId);

            tid = (DWORD_PTR)spi->ti[j].ClientId.UniqueThread;
            ok(!(tid & 3), "Unexpected TID low bits: %p\n", spi->ti[j].ClientId.UniqueThread);
        }

        if (!spi->NextEntryOffset) break;

        one_before_last_pid = last_pid;

        spi = (SYSTEM_PROCESS_INFORMATION_PRIVATE*)((char*)spi + spi->NextEntryOffset);
    }
    if (winetest_debug > 1) trace("%u processes, %u threads\n", i, k);

    if (one_before_last_pid == 0) one_before_last_pid = last_pid;

    HeapFree( GetProcessHeap(), 0, spi_buf);

    for (i = 1; i < 4; ++i)
    {
        InitializeObjectAttributes( &attr, NULL, 0, NULL, NULL );
        cid.UniqueProcess = ULongToHandle(GetCurrentProcessId() + i);
        cid.UniqueThread = 0;

        status = NtOpenProcess( &handle, PROCESS_QUERY_LIMITED_INFORMATION, &attr, &cid );
        ok( status == STATUS_SUCCESS || broken( status == STATUS_ACCESS_DENIED ) /* wxppro */,
            "NtOpenProcess returned:%x\n", status );
        if (status != STATUS_SUCCESS) continue;

        status = pNtQueryInformationProcess( handle, ProcessBasicInformation, &pbi, sizeof(pbi), NULL );
        ok( status == STATUS_SUCCESS, "NtQueryInformationProcess returned:%x\n", status );
        ok( pbi.UniqueProcessId == GetCurrentProcessId(),
            "Expected pid %p, got %p\n", ULongToHandle(GetCurrentProcessId()), ULongToHandle(pbi.UniqueProcessId) );

        NtClose( handle );
    }

    for (i = 1; i < 4; ++i)
    {
        InitializeObjectAttributes( &attr, NULL, 0, NULL, NULL );
        cid.UniqueProcess = 0;
        cid.UniqueThread = ULongToHandle(GetCurrentThreadId() + i);

        status = NtOpenThread( &handle, THREAD_QUERY_LIMITED_INFORMATION, &attr, &cid );
        ok( status == STATUS_SUCCESS || broken( status == STATUS_ACCESS_DENIED ) /* wxppro */,
            "NtOpenThread returned:%x\n", status );
        if (status != STATUS_SUCCESS) continue;

        status = pNtQueryInformationThread( handle, ThreadBasicInformation, &tbi, sizeof(tbi), NULL );
        ok( status == STATUS_SUCCESS, "NtQueryInformationThread returned:%x\n", status );
        ok( tbi.ClientId.UniqueThread == ULongToHandle(GetCurrentThreadId()),
            "Expected tid %p, got %p\n", ULongToHandle(GetCurrentThreadId()), tbi.ClientId.UniqueThread );

        NtClose( handle );
    }
}

static void test_query_procperf(void)
{
    NTSTATUS status;
    ULONG ReturnLength;
    ULONG NeededLength;
    SYSTEM_BASIC_INFORMATION sbi;
    SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION* sppi;

    /* Find out the number of processors */
    status = pNtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof(sbi), &ReturnLength);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    NeededLength = sbi.NumberOfProcessors * sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION);

    sppi = HeapAlloc(GetProcessHeap(), 0, NeededLength);

    status = pNtQuerySystemInformation(SystemProcessorPerformanceInformation, sppi, 0, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    /* Try it for 1 processor */
    sppi->KernelTime.QuadPart = 0xdeaddead;
    sppi->UserTime.QuadPart = 0xdeaddead;
    sppi->IdleTime.QuadPart = 0xdeaddead;
    status = pNtQuerySystemInformation(SystemProcessorPerformanceInformation, sppi,
                                       sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION), &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) == ReturnLength,
        "Inconsistent length %d\n", ReturnLength);
    ok (sppi->KernelTime.QuadPart != 0xdeaddead, "KernelTime unchanged\n");
    ok (sppi->UserTime.QuadPart != 0xdeaddead, "UserTime unchanged\n");
    ok (sppi->IdleTime.QuadPart != 0xdeaddead, "IdleTime unchanged\n");

    /* Try it for all processors */
    sppi->KernelTime.QuadPart = 0xdeaddead;
    sppi->UserTime.QuadPart = 0xdeaddead;
    sppi->IdleTime.QuadPart = 0xdeaddead;
    status = pNtQuerySystemInformation(SystemProcessorPerformanceInformation, sppi, NeededLength, &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( NeededLength == ReturnLength, "Inconsistent length (%d) <-> (%d)\n", NeededLength, ReturnLength);
    ok (sppi->KernelTime.QuadPart != 0xdeaddead, "KernelTime unchanged\n");
    ok (sppi->UserTime.QuadPart != 0xdeaddead, "UserTime unchanged\n");
    ok (sppi->IdleTime.QuadPart != 0xdeaddead, "IdleTime unchanged\n");

    /* A too large given buffer size */
    sppi = HeapReAlloc(GetProcessHeap(), 0, sppi , NeededLength + 2);
    sppi->KernelTime.QuadPart = 0xdeaddead;
    sppi->UserTime.QuadPart = 0xdeaddead;
    sppi->IdleTime.QuadPart = 0xdeaddead;
    status = pNtQuerySystemInformation(SystemProcessorPerformanceInformation, sppi, NeededLength + 2, &ReturnLength);
    ok( status == STATUS_SUCCESS || status == STATUS_INFO_LENGTH_MISMATCH /* vista */,
        "Expected STATUS_SUCCESS or STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);
    ok( NeededLength == ReturnLength, "Inconsistent length (%d) <-> (%d)\n", NeededLength, ReturnLength);
    if (status == STATUS_SUCCESS)
    {
        ok (sppi->KernelTime.QuadPart != 0xdeaddead, "KernelTime unchanged\n");
        ok (sppi->UserTime.QuadPart != 0xdeaddead, "UserTime unchanged\n");
        ok (sppi->IdleTime.QuadPart != 0xdeaddead, "IdleTime unchanged\n");
    }
    else /* vista and 2008 */
    {
        ok (sppi->KernelTime.QuadPart == 0xdeaddead, "KernelTime changed\n");
        ok (sppi->UserTime.QuadPart == 0xdeaddead, "UserTime changed\n");
        ok (sppi->IdleTime.QuadPart == 0xdeaddead, "IdleTime changed\n");
    }

    HeapFree( GetProcessHeap(), 0, sppi);
}

static void test_query_module(void)
{
    const RTL_PROCESS_MODULE_INFORMATION_EX *infoex;
    SYSTEM_MODULE_INFORMATION *info;
    NTSTATUS status;
    ULONG size, i;
    char *buffer;

    status = pNtQuerySystemInformation(SystemModuleInformation, NULL, 0, &size);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "got %#x\n", status);
    ok(size > 0, "expected nonzero size\n");

    info = malloc(size);
    status = pNtQuerySystemInformation(SystemModuleInformation, info, size, &size);
    ok(!status, "got %#x\n", status);

    ok(info->ModulesCount > 0, "Expected some modules to be loaded\n");

    for (i = 0; i < info->ModulesCount; i++)
    {
        const SYSTEM_MODULE *module = &info->Modules[i];

        ok(module->LoadOrderIndex == i, "%u: got index %u\n", i, module->LoadOrderIndex);
        ok(module->ImageBaseAddress || is_wow64, "%u: got NULL address for %s\n", i, module->Name);
        ok(module->ImageSize, "%u: got 0 size\n", i);
        ok(module->LoadCount, "%u: got 0 load count\n", i);
    }

    free(info);

    status = pNtQuerySystemInformation(SystemModuleInformationEx, NULL, 0, &size);
    if (status == STATUS_INVALID_INFO_CLASS)
    {
        win_skip("SystemModuleInformationEx is not supported.\n");
        return;
    }
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "got %#x\n", status);
    ok(size > 0, "expected nonzero size\n");

    buffer = malloc(size);
    status = pNtQuerySystemInformation(SystemModuleInformationEx, buffer, size, &size);
    ok(!status, "got %#x\n", status);

    infoex = (const void *)buffer;
    for (i = 0; infoex->NextOffset; i++)
    {
        const SYSTEM_MODULE *module = &infoex->BaseInfo;

        ok(module->LoadOrderIndex == i, "%u: got index %u\n", i, module->LoadOrderIndex);
        ok(module->ImageBaseAddress || is_wow64, "%u: got NULL address for %s\n", i, module->Name);
        ok(module->ImageSize, "%u: got 0 size\n", i);
        ok(module->LoadCount, "%u: got 0 load count\n", i);

        infoex = (const void *)((const char *)infoex + infoex->NextOffset);
    }
    ok(((char *)infoex - buffer) + sizeof(infoex->NextOffset) == size,
            "got size %u, null terminator %u\n", size, (char *)infoex - buffer);

    free(buffer);

}

static void test_query_handle(void)
{
    NTSTATUS status;
    ULONG ExpectedLength, ReturnLength;
    ULONG SystemInformationLength = sizeof(SYSTEM_HANDLE_INFORMATION);
    SYSTEM_HANDLE_INFORMATION* shi = HeapAlloc(GetProcessHeap(), 0, SystemInformationLength);
    HANDLE EventHandle;
    BOOL found;
    INT i;

    EventHandle = CreateEventA(NULL, FALSE, FALSE, NULL);
    ok( EventHandle != NULL, "CreateEventA failed %u\n", GetLastError() );

    /* Request the needed length : a SystemInformationLength greater than one struct sets ReturnLength */
    ReturnLength = 0xdeadbeef;
    status = pNtQuerySystemInformation(SystemHandleInformation, shi, SystemInformationLength, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);
    ok( ReturnLength != 0xdeadbeef, "Expected valid ReturnLength\n" );

    SystemInformationLength = ReturnLength;
    shi = HeapReAlloc(GetProcessHeap(), 0, shi , SystemInformationLength);
    memset(shi, 0x55, SystemInformationLength);

    ReturnLength = 0xdeadbeef;
    status = pNtQuerySystemInformation(SystemHandleInformation, shi, SystemInformationLength, &ReturnLength);
    while (status == STATUS_INFO_LENGTH_MISMATCH) /* Vista / 2008 */
    {
        SystemInformationLength *= 2;
        shi = HeapReAlloc(GetProcessHeap(), 0, shi, SystemInformationLength);
        memset(shi, 0x55, SystemInformationLength);
        status = pNtQuerySystemInformation(SystemHandleInformation, shi, SystemInformationLength, &ReturnLength);
    }
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status );
    ExpectedLength = FIELD_OFFSET(SYSTEM_HANDLE_INFORMATION, Handle[shi->Count]);
    ok( ReturnLength == ExpectedLength || broken(ReturnLength == ExpectedLength - sizeof(DWORD)), /* Vista / 2008 */
        "Expected length %u, got %u\n", ExpectedLength, ReturnLength );
    ok( shi->Count > 1, "Expected more than 1 handle, got %u\n", shi->Count );
    ok( shi->Handle[1].HandleValue != 0x5555 || broken( shi->Handle[1].HandleValue == 0x5555 ), /* Vista / 2008 */
        "Uninitialized second handle\n" );
    if (shi->Handle[1].HandleValue == 0x5555)
    {
        win_skip("Skipping broken SYSTEM_HANDLE_INFORMATION\n");
        CloseHandle(EventHandle);
        goto done;
    }

    for (i = 0, found = FALSE; i < shi->Count && !found; i++)
        found = (shi->Handle[i].OwnerPid == GetCurrentProcessId()) &&
                ((HANDLE)(ULONG_PTR)shi->Handle[i].HandleValue == EventHandle);
    ok( found, "Expected to find event handle %p (pid %x) in handle list\n", EventHandle, GetCurrentProcessId() );

    CloseHandle(EventHandle);

    ReturnLength = 0xdeadbeef;
    status = pNtQuerySystemInformation(SystemHandleInformation, shi, SystemInformationLength, &ReturnLength);
    while (status == STATUS_INFO_LENGTH_MISMATCH) /* Vista / 2008 */
    {
        SystemInformationLength *= 2;
        shi = HeapReAlloc(GetProcessHeap(), 0, shi, SystemInformationLength);
        status = pNtQuerySystemInformation(SystemHandleInformation, shi, SystemInformationLength, &ReturnLength);
    }
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status );
    for (i = 0, found = FALSE; i < shi->Count && !found; i++)
        found = (shi->Handle[i].OwnerPid == GetCurrentProcessId()) &&
                ((HANDLE)(ULONG_PTR)shi->Handle[i].HandleValue == EventHandle);
    ok( !found, "Unexpectedly found event handle in handle list\n" );

    status = pNtQuerySystemInformation(SystemHandleInformation, NULL, SystemInformationLength, &ReturnLength);
    ok( status == STATUS_ACCESS_VIOLATION, "Expected STATUS_ACCESS_VIOLATION, got %08x\n", status );

done:
    HeapFree( GetProcessHeap(), 0, shi);
}

static void test_query_cache(void)
{
    NTSTATUS status;
    ULONG ReturnLength;
    BYTE buffer[128];
    SYSTEM_CACHE_INFORMATION *sci = (SYSTEM_CACHE_INFORMATION *) buffer;
    ULONG expected;
    INT i;

    /* the large SYSTEM_CACHE_INFORMATION on WIN64 is not documented */
    expected = sizeof(SYSTEM_CACHE_INFORMATION);
    for (i = sizeof(buffer); i>= expected; i--)
    {
        ReturnLength = 0xdeadbeef;
        status = pNtQuerySystemInformation(SystemCacheInformation, sci, i, &ReturnLength);
        ok(!status && (ReturnLength == expected),
            "%d: got 0x%x and %u (expected STATUS_SUCCESS and %u)\n", i, status, ReturnLength, expected);
    }

    /* buffer too small for the full result.
       Up to win7, the function succeeds with a partial result. */
    status = pNtQuerySystemInformation(SystemCacheInformation, sci, i, &ReturnLength);
    if (!status)
    {
        expected = offsetof(SYSTEM_CACHE_INFORMATION, MinimumWorkingSet);
        for (; i>= expected; i--)
        {
            ReturnLength = 0xdeadbeef;
            status = pNtQuerySystemInformation(SystemCacheInformation, sci, i, &ReturnLength);
            ok(!status && (ReturnLength == expected),
                "%d: got 0x%x and %u (expected STATUS_SUCCESS and %u)\n", i, status, ReturnLength, expected);
        }
    }

    /* buffer too small for the result, this call will always fail */
    ReturnLength = 0xdeadbeef;
    status = pNtQuerySystemInformation(SystemCacheInformation, sci, i, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH &&
        ((ReturnLength == expected) || broken(!ReturnLength) || broken(ReturnLength == 0xfffffff0)),
        "%d: got 0x%x and %u (expected STATUS_INFO_LENGTH_MISMATCH and %u)\n", i, status, ReturnLength, expected);

    if (0) {
        /* this crashes on some vista / win7 machines */
        ReturnLength = 0xdeadbeef;
        status = pNtQuerySystemInformation(SystemCacheInformation, sci, 0, &ReturnLength);
        ok( status == STATUS_INFO_LENGTH_MISMATCH &&
            ((ReturnLength == expected) || broken(!ReturnLength) || broken(ReturnLength == 0xfffffff0)),
            "0: got 0x%x and %u (expected STATUS_INFO_LENGTH_MISMATCH and %u)\n", status, ReturnLength, expected);
    }
}

static void test_query_interrupt(void)
{
    NTSTATUS status;
    ULONG ReturnLength;
    ULONG NeededLength;
    SYSTEM_BASIC_INFORMATION sbi;
    SYSTEM_INTERRUPT_INFORMATION* sii;

    /* Find out the number of processors */
    status = pNtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof(sbi), &ReturnLength);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    NeededLength = sbi.NumberOfProcessors * sizeof(SYSTEM_INTERRUPT_INFORMATION);

    sii = HeapAlloc(GetProcessHeap(), 0, NeededLength);

    status = pNtQuerySystemInformation(SystemInterruptInformation, sii, 0, &ReturnLength);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);
    ok(ReturnLength == NeededLength, "got %u\n", ReturnLength);

    /* Try it for all processors */
    status = pNtQuerySystemInformation(SystemInterruptInformation, sii, NeededLength, &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);

    /* Windows XP and W2K3 (and others?) always return 0 for the ReturnLength
     * No test added for this as it's highly unlikely that an app depends on this
    */

    HeapFree( GetProcessHeap(), 0, sii);
}

static void test_time_adjustment(void)
{
    SYSTEM_TIME_ADJUSTMENT_QUERY query;
    SYSTEM_TIME_ADJUSTMENT adjust;
    NTSTATUS status;
    ULONG len;

    memset( &query, 0xcc, sizeof(query) );
    status = pNtQuerySystemInformation( SystemTimeAdjustmentInformation, &query, sizeof(query), &len );
    ok( status == STATUS_SUCCESS, "got %08x\n", status );
    ok( len == sizeof(query) || broken(!len) /* winxp */, "wrong len %u\n", len );
    ok( query.TimeAdjustmentDisabled == TRUE || query.TimeAdjustmentDisabled == FALSE,
        "wrong value %x\n", query.TimeAdjustmentDisabled );

    status = pNtQuerySystemInformation( SystemTimeAdjustmentInformation, &query, sizeof(query)-1, &len );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "got %08x\n", status );
    ok( len == sizeof(query) || broken(!len) /* winxp */, "wrong len %u\n", len );

    status = pNtQuerySystemInformation( SystemTimeAdjustmentInformation, &query, sizeof(query)+1, &len );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "got %08x\n", status );
    ok( len == sizeof(query) || broken(!len) /* winxp */, "wrong len %u\n", len );

    adjust.TimeAdjustment = query.TimeAdjustment;
    adjust.TimeAdjustmentDisabled = query.TimeAdjustmentDisabled;
    status = pNtSetSystemInformation( SystemTimeAdjustmentInformation, &adjust, sizeof(adjust) );
    ok( status == STATUS_SUCCESS || status == STATUS_PRIVILEGE_NOT_HELD, "got %08x\n", status );
    status = pNtSetSystemInformation( SystemTimeAdjustmentInformation, &adjust, sizeof(adjust)-1 );
    todo_wine
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "got %08x\n", status );
    status = pNtSetSystemInformation( SystemTimeAdjustmentInformation, &adjust, sizeof(adjust)+1 );
    todo_wine
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "got %08x\n", status );
}

static void test_query_kerndebug(void)
{
    NTSTATUS status;
    ULONG ReturnLength;
    SYSTEM_KERNEL_DEBUGGER_INFORMATION skdi;

    status = pNtQuerySystemInformation(SystemKernelDebuggerInformation, &skdi, 0, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    status = pNtQuerySystemInformation(SystemKernelDebuggerInformation, &skdi, sizeof(skdi), &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( sizeof(skdi) == ReturnLength, "Inconsistent length %d\n", ReturnLength);

    status = pNtQuerySystemInformation(SystemKernelDebuggerInformation, &skdi, sizeof(skdi) + 2, &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( sizeof(skdi) == ReturnLength, "Inconsistent length %d\n", ReturnLength);
}

static void test_query_regquota(void)
{
    NTSTATUS status;
    ULONG ReturnLength;
    SYSTEM_REGISTRY_QUOTA_INFORMATION srqi;

    status = pNtQuerySystemInformation(SystemRegistryQuotaInformation, &srqi, 0, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    status = pNtQuerySystemInformation(SystemRegistryQuotaInformation, &srqi, sizeof(srqi), &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( sizeof(srqi) == ReturnLength, "Inconsistent length %d\n", ReturnLength);

    status = pNtQuerySystemInformation(SystemRegistryQuotaInformation, &srqi, sizeof(srqi) + 2, &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( sizeof(srqi) == ReturnLength, "Inconsistent length %d\n", ReturnLength);
}

static void test_query_logicalproc(void)
{
    NTSTATUS status;
    ULONG len, i, proc_no;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *slpi;
    SYSTEM_INFO si;

    GetSystemInfo(&si);

    status = pNtQuerySystemInformation(SystemLogicalProcessorInformation, NULL, 0, &len);
    if (status == STATUS_INVALID_INFO_CLASS) /* wow64 win8+ */
    {
        win_skip("SystemLogicalProcessorInformation is not supported\n");
        return;
    }
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);
    ok(len%sizeof(*slpi) == 0, "Incorrect length %d\n", len);

    slpi = HeapAlloc(GetProcessHeap(), 0, len);
    status = pNtQuerySystemInformation(SystemLogicalProcessorInformation, slpi, len, &len);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);

    proc_no = 0;
    for(i=0; i<len/sizeof(*slpi); i++) {
        switch(slpi[i].Relationship) {
        case RelationProcessorCore:
            /* Get number of logical processors */
            for(; slpi[i].ProcessorMask; slpi[i].ProcessorMask /= 2)
                proc_no += slpi[i].ProcessorMask%2;
            break;
        default:
            break;
        }
    }
    ok(proc_no > 0, "No processors were found\n");
    if(si.dwNumberOfProcessors <= 32)
        ok(proc_no == si.dwNumberOfProcessors, "Incorrect number of logical processors: %d, expected %d\n",
                proc_no, si.dwNumberOfProcessors);

    HeapFree(GetProcessHeap(), 0, slpi);
}

static void test_query_logicalprocex(void)
{
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *infoex, *infoex_public, *infoex_core, *infoex_numa,
                                            *infoex_cache, *infoex_package, *infoex_group, *ex;
    DWORD relationship, len, len_public, len_core, len_numa, len_cache, len_package, len_group, len_union;
    unsigned int i, j;
    NTSTATUS status;
    BOOL ret;

    if (!pNtQuerySystemInformationEx)
        return;

    len = 0;
    relationship = RelationAll;
    status = pNtQuerySystemInformationEx(SystemLogicalProcessorInformationEx, &relationship, sizeof(relationship), NULL, 0, &len);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "got 0x%08x\n", status);
    ok(len > 0, "got %u\n", len);

    len_core = 0;
    relationship = RelationProcessorCore;
    status = pNtQuerySystemInformationEx(SystemLogicalProcessorInformationEx, &relationship, sizeof(relationship), NULL, 0, &len_core);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "got 0x%08x\n", status);
    ok(len_core > 0, "got %u\n", len_core);

    len_numa = 0;
    relationship = RelationNumaNode;
    status = pNtQuerySystemInformationEx(SystemLogicalProcessorInformationEx, &relationship, sizeof(relationship), NULL, 0, &len_numa);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "got 0x%08x\n", status);
    ok(len_numa > 0, "got %u\n", len_numa);

    len_cache = 0;
    relationship = RelationCache;
    status = pNtQuerySystemInformationEx(SystemLogicalProcessorInformationEx, &relationship, sizeof(relationship), NULL, 0, &len_cache);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "got 0x%08x\n", status);
    ok(len_cache > 0, "got %u\n", len_cache);

    len_package = 0;
    relationship = RelationProcessorPackage;
    status = pNtQuerySystemInformationEx(SystemLogicalProcessorInformationEx, &relationship, sizeof(relationship), NULL, 0, &len_package);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "got 0x%08x\n", status);
    ok(len_package > 0, "got %u\n", len_package);

    len_group = 0;
    relationship = RelationGroup;
    status = pNtQuerySystemInformationEx(SystemLogicalProcessorInformationEx, &relationship, sizeof(relationship), NULL, 0, &len_group);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "got 0x%08x\n", status);
    ok(len_group > 0, "got %u\n", len_group);

    len_public = 0;
    ret = pGetLogicalProcessorInformationEx(RelationAll, NULL, &len_public);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "got %d, error %d\n", ret, GetLastError());
    ok(len == len_public, "got %u, expected %u\n", len_public, len);

    infoex = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
    infoex_public = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len_public);
    infoex_core = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len_core);
    infoex_numa = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len_numa);
    infoex_cache = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len_cache);
    infoex_package = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len_package);
    infoex_group = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len_group);

    relationship = RelationAll;
    status = pNtQuerySystemInformationEx(SystemLogicalProcessorInformationEx, &relationship, sizeof(relationship), infoex, len, &len);
    ok(status == STATUS_SUCCESS, "got 0x%08x\n", status);

    ret = pGetLogicalProcessorInformationEx(RelationAll, infoex_public, &len_public);
    ok(ret, "got %d, error %d\n", ret, GetLastError());
    ok(!memcmp(infoex, infoex_public, len), "returned info data mismatch\n");

    /* Test for RelationAll. */
    for (i = 0; status == STATUS_SUCCESS && i < len; )
    {
        ex = (void *)(((char *)infoex) + i);
        ok(ex->Size, "%u: got size 0\n", i);

        if (winetest_debug <= 1)
        {
            i += ex->Size;
            continue;
        }

        trace("infoex[%u].Size: %u\n", i, ex->Size);
        switch (ex->Relationship)
        {
        case RelationProcessorCore:
        case RelationProcessorPackage:
            trace("infoex[%u].Relationship: 0x%x (%s)\n", i, ex->Relationship, ex->Relationship == RelationProcessorCore ? "Core" : "Package");
            trace("infoex[%u].Processor.Flags: 0x%x\n", i, ex->Processor.Flags);
            trace("infoex[%u].Processor.EfficiencyClass: 0x%x\n", i, ex->Processor.EfficiencyClass);
            trace("infoex[%u].Processor.GroupCount: 0x%x\n", i, ex->Processor.GroupCount);
            for (j = 0; j < ex->Processor.GroupCount; ++j)
            {
                trace("infoex[%u].Processor.GroupMask[%u].Mask: 0x%lx\n", i, j, ex->Processor.GroupMask[j].Mask);
                trace("infoex[%u].Processor.GroupMask[%u].Group: 0x%x\n", i, j, ex->Processor.GroupMask[j].Group);
            }
            break;
        case RelationNumaNode:
            trace("infoex[%u].Relationship: 0x%x (NumaNode)\n", i, ex->Relationship);
            trace("infoex[%u].NumaNode.NodeNumber: 0x%x\n", i, ex->NumaNode.NodeNumber);
            trace("infoex[%u].NumaNode.GroupMask.Mask: 0x%lx\n", i, ex->NumaNode.GroupMask.Mask);
            trace("infoex[%u].NumaNode.GroupMask.Group: 0x%x\n", i, ex->NumaNode.GroupMask.Group);
            break;
        case RelationCache:
            trace("infoex[%u].Relationship: 0x%x (Cache)\n", i, ex->Relationship);
            trace("infoex[%u].Cache.Level: 0x%x\n", i, ex->Cache.Level);
            trace("infoex[%u].Cache.Associativity: 0x%x\n", i, ex->Cache.Associativity);
            trace("infoex[%u].Cache.LineSize: 0x%x\n", i, ex->Cache.LineSize);
            trace("infoex[%u].Cache.CacheSize: 0x%x\n", i, ex->Cache.CacheSize);
            trace("infoex[%u].Cache.Type: 0x%x\n", i, ex->Cache.Type);
            trace("infoex[%u].Cache.GroupMask.Mask: 0x%lx\n", i, ex->Cache.GroupMask.Mask);
            trace("infoex[%u].Cache.GroupMask.Group: 0x%x\n", i, ex->Cache.GroupMask.Group);
            break;
        case RelationGroup:
            trace("infoex[%u].Relationship: 0x%x (Group)\n", i, ex->Relationship);
            trace("infoex[%u].Group.MaximumGroupCount: 0x%x\n", i, ex->Group.MaximumGroupCount);
            trace("infoex[%u].Group.ActiveGroupCount: 0x%x\n", i, ex->Group.ActiveGroupCount);
            for (j = 0; j < ex->Group.ActiveGroupCount; ++j)
            {
                trace("infoex[%u].Group.GroupInfo[%u].MaximumProcessorCount: 0x%x\n", i, j, ex->Group.GroupInfo[j].MaximumProcessorCount);
                trace("infoex[%u].Group.GroupInfo[%u].ActiveProcessorCount: 0x%x\n", i, j, ex->Group.GroupInfo[j].ActiveProcessorCount);
                trace("infoex[%u].Group.GroupInfo[%u].ActiveProcessorMask: 0x%lx\n", i, j, ex->Group.GroupInfo[j].ActiveProcessorMask);
            }
            break;
        default:
            ok(0, "Got invalid relationship value: 0x%x\n", ex->Relationship);
            break;
        }

        i += ex->Size;
    }

    /* Test Relationship filtering. */

    relationship = RelationProcessorCore;
    status = pNtQuerySystemInformationEx(SystemLogicalProcessorInformationEx, &relationship, sizeof(relationship), infoex_core, len_core, &len_core);
    ok(status == STATUS_SUCCESS, "got 0x%08x\n", status);

    for (i = 0; status == STATUS_SUCCESS && i < len_core;)
    {
        ex = (void *)(((char*)infoex_core) + i);
        ok(ex->Size, "%u: got size 0\n", i);
        ok(ex->Relationship == RelationProcessorCore, "%u: got relationship %#x\n", i, ex->Relationship);
        i += ex->Size;
    }

    relationship = RelationNumaNode;
    status = pNtQuerySystemInformationEx(SystemLogicalProcessorInformationEx, &relationship, sizeof(relationship), infoex_numa, len_numa, &len_numa);
    ok(status == STATUS_SUCCESS, "got 0x%08x\n", status);

    for (i = 0; status == STATUS_SUCCESS && i < len_numa;)
    {
        ex = (void *)(((char*)infoex_numa) + i);
        ok(ex->Size, "%u: got size 0\n", i);
        ok(ex->Relationship == RelationNumaNode, "%u: got relationship %#x\n", i, ex->Relationship);
        i += ex->Size;
    }

    relationship = RelationCache;
    status = pNtQuerySystemInformationEx(SystemLogicalProcessorInformationEx, &relationship, sizeof(relationship), infoex_cache, len_cache, &len_cache);
    ok(status == STATUS_SUCCESS, "got 0x%08x\n", status);

    for (i = 0; status == STATUS_SUCCESS && i < len_cache;)
    {
        ex = (void *)(((char*)infoex_cache) + i);
        ok(ex->Size, "%u: got size 0\n", i);
        ok(ex->Relationship == RelationCache, "%u: got relationship %#x\n", i, ex->Relationship);
        i += ex->Size;
    }

    relationship = RelationProcessorPackage;
    status = pNtQuerySystemInformationEx(SystemLogicalProcessorInformationEx, &relationship, sizeof(relationship), infoex_package, len_package, &len_package);
    ok(status == STATUS_SUCCESS, "got 0x%08x\n", status);

    for (i = 0; status == STATUS_SUCCESS && i < len_package;)
    {
        ex = (void *)(((char*)infoex_package) + i);
        ok(ex->Size, "%u: got size 0\n", i);
        ok(ex->Relationship == RelationProcessorPackage, "%u: got relationship %#x\n", i, ex->Relationship);
        i += ex->Size;
    }

    relationship = RelationGroup;
    status = pNtQuerySystemInformationEx(SystemLogicalProcessorInformationEx, &relationship, sizeof(relationship), infoex_group, len_group, &len_group);
    ok(status == STATUS_SUCCESS, "got 0x%08x\n", status);

    for (i = 0; status == STATUS_SUCCESS && i < len_group;)
    {
        ex = (void *)(((char *)infoex_group) + i);
        ok(ex->Size, "%u: got size 0\n", i);
        ok(ex->Relationship == RelationGroup, "%u: got relationship %#x\n", i, ex->Relationship);
        i += ex->Size;
    }

    len_union = len_core + len_numa + len_cache + len_package + len_group;
    ok(len == len_union, "Expected 0x%x, got 0x%0x\n", len, len_union);

    HeapFree(GetProcessHeap(), 0, infoex);
    HeapFree(GetProcessHeap(), 0, infoex_public);
    HeapFree(GetProcessHeap(), 0, infoex_core);
    HeapFree(GetProcessHeap(), 0, infoex_numa);
    HeapFree(GetProcessHeap(), 0, infoex_cache);
    HeapFree(GetProcessHeap(), 0, infoex_package);
    HeapFree(GetProcessHeap(), 0, infoex_group);
}

static void test_query_firmware(void)
{
    static const ULONG min_sfti_len = FIELD_OFFSET(SYSTEM_FIRMWARE_TABLE_INFORMATION, TableBuffer);
    ULONG len1, len2;
    NTSTATUS status;
    SYSTEM_FIRMWARE_TABLE_INFORMATION *sfti;

    sfti = HeapAlloc(GetProcessHeap(), 0, min_sfti_len);
    ok(!!sfti, "Failed to allocate memory\n");

    sfti->ProviderSignature = 0;
    sfti->Action = 0;
    sfti->TableID = 0;

    status = pNtQuerySystemInformation(SystemFirmwareTableInformation, sfti, min_sfti_len - 1, &len1);
    ok(status == STATUS_INFO_LENGTH_MISMATCH || broken(status == STATUS_INVALID_INFO_CLASS) /* xp */,
       "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);
    if (len1 == 0) /* xp, 2003 */
    {
        win_skip("SystemFirmwareTableInformation is not available\n");
        HeapFree(GetProcessHeap(), 0, sfti);
        return;
    }
    ok(len1 == min_sfti_len, "Expected length %u, got %u\n", min_sfti_len, len1);

    status = pNtQuerySystemInformation(SystemFirmwareTableInformation, sfti, min_sfti_len, &len1);
    ok(status == STATUS_NOT_IMPLEMENTED, "Expected STATUS_NOT_IMPLEMENTED, got %08x\n", status);
    ok(len1 == 0, "Expected length 0, got %u\n", len1);

    sfti->ProviderSignature = RSMB;
    sfti->Action = SystemFirmwareTable_Get;

    status = pNtQuerySystemInformation(SystemFirmwareTableInformation, sfti, min_sfti_len, &len1);
    ok(status == STATUS_BUFFER_TOO_SMALL, "Expected STATUS_BUFFER_TOO_SMALL, got %08x\n", status);
    ok(len1 >= min_sfti_len, "Expected length >= %u, got %u\n", min_sfti_len, len1);
    ok(sfti->TableBufferLength == len1 - min_sfti_len,
       "Expected length %u, got %u\n", len1 - min_sfti_len, sfti->TableBufferLength);

    sfti = HeapReAlloc(GetProcessHeap(), 0, sfti, len1);
    ok(!!sfti, "Failed to allocate memory\n");

    status = pNtQuerySystemInformation(SystemFirmwareTableInformation, sfti, len1, &len2);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok(len2 == len1, "Expected length %u, got %u\n", len1, len2);
    ok(sfti->TableBufferLength == len1 - min_sfti_len,
       "Expected length %u, got %u\n", len1 - min_sfti_len, sfti->TableBufferLength);

    HeapFree(GetProcessHeap(), 0, sfti);
}

static void test_query_battery(void)
{
    SYSTEM_BATTERY_STATE bs;
    NTSTATUS status;
    DWORD time_left;

    memset(&bs, 0x23, sizeof(bs));
    status = NtPowerInformation(SystemBatteryState, NULL, 0, &bs, sizeof(bs));
    if (status == STATUS_NOT_IMPLEMENTED)
    {
        skip("SystemBatteryState not implemented\n");
        return;
    }
    ok(status == STATUS_SUCCESS, "expected success\n");

    if (winetest_debug > 1)
    {
        trace("Battery state:\n");
        trace("AcOnLine          : %u\n", bs.AcOnLine);
        trace("BatteryPresent    : %u\n", bs.BatteryPresent);
        trace("Charging          : %u\n", bs.Charging);
        trace("Discharging       : %u\n", bs.Discharging);
        trace("Tag               : %u\n", bs.Tag);
        trace("MaxCapacity       : %u\n", bs.MaxCapacity);
        trace("RemainingCapacity : %u\n", bs.RemainingCapacity);
        trace("Rate              : %d\n", (LONG)bs.Rate);
        trace("EstimatedTime     : %u\n", bs.EstimatedTime);
        trace("DefaultAlert1     : %u\n", bs.DefaultAlert1);
        trace("DefaultAlert2     : %u\n", bs.DefaultAlert2);
    }

    ok(bs.MaxCapacity >= bs.RemainingCapacity,
       "expected MaxCapacity %u to be greater than or equal to RemainingCapacity %u\n",
       bs.MaxCapacity, bs.RemainingCapacity);

    if (!bs.BatteryPresent)
        time_left = 0;
    else if (!bs.Charging && (LONG)bs.Rate < 0)
        time_left = 3600 * bs.RemainingCapacity / -(LONG)bs.Rate;
    else
        time_left = ~0u;
    ok(bs.EstimatedTime == time_left,
       "expected %u minutes remaining got %u minutes\n", time_left, bs.EstimatedTime);
}

static void test_query_processor_power_info(void)
{
    NTSTATUS status;
    PROCESSOR_POWER_INFORMATION* ppi;
    ULONG size;
    SYSTEM_INFO si;
    int i;

    GetSystemInfo(&si);
    size = si.dwNumberOfProcessors * sizeof(PROCESSOR_POWER_INFORMATION);
    ppi = HeapAlloc(GetProcessHeap(), 0, size);

    /* If size < (sizeof(PROCESSOR_POWER_INFORMATION) * NumberOfProcessors), Win7 returns
     * STATUS_BUFFER_TOO_SMALL. WinXP returns STATUS_SUCCESS for any value of size.  It copies as
     * many whole PROCESSOR_POWER_INFORMATION structures that there is room for.  Even if there is
     * not enough room for one structure, WinXP still returns STATUS_SUCCESS having done nothing.
     *
     * If ppi == NULL, Win7 returns STATUS_INVALID_PARAMETER while WinXP returns STATUS_SUCCESS
     * and does nothing.
     *
     * The same behavior is seen with CallNtPowerInformation (in powrprof.dll).
     */

    if (si.dwNumberOfProcessors > 1)
    {
        for(i = 0; i < si.dwNumberOfProcessors; i++)
            ppi[i].Number = 0xDEADBEEF;

        /* Call with a buffer size that is large enough to hold at least one but not large
         * enough to hold them all.  This will be STATUS_SUCCESS on WinXP but not on Win7 */
        status = pNtPowerInformation(ProcessorInformation, 0, 0, ppi, size - sizeof(PROCESSOR_POWER_INFORMATION));
        if (status == STATUS_SUCCESS)
        {
            /* lax version found on older Windows like WinXP */
            ok( (ppi[si.dwNumberOfProcessors - 2].Number != 0xDEADBEEF) &&
                (ppi[si.dwNumberOfProcessors - 1].Number == 0xDEADBEEF),
                "Expected all but the last record to be overwritten.\n");

            status = pNtPowerInformation(ProcessorInformation, 0, 0, 0, size);
            ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);

            for(i = 0; i < si.dwNumberOfProcessors; i++)
                ppi[i].Number = 0xDEADBEEF;
            status = pNtPowerInformation(ProcessorInformation, 0, 0, ppi, sizeof(PROCESSOR_POWER_INFORMATION) - 1);
            ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
            for(i = 0; i < si.dwNumberOfProcessors; i++)
                if (ppi[i].Number != 0xDEADBEEF) break;
            ok( i == si.dwNumberOfProcessors, "Expected untouched buffer\n");
        }
        else
        {
            /* picky version found on newer Windows like Win7 */
            ok( ppi[1].Number == 0xDEADBEEF, "Expected untouched buffer.\n");
            ok( status == STATUS_BUFFER_TOO_SMALL, "Expected STATUS_BUFFER_TOO_SMALL, got %08x\n", status);

            status = pNtPowerInformation(ProcessorInformation, 0, 0, 0, size);
            ok( status == STATUS_SUCCESS || status == STATUS_INVALID_PARAMETER, "Got %08x\n", status);

            status = pNtPowerInformation(ProcessorInformation, 0, 0, ppi, 0);
            ok( status == STATUS_BUFFER_TOO_SMALL || status == STATUS_INVALID_PARAMETER, "Got %08x\n", status);
        }
    }
    else
    {
        skip("Test needs more than one processor.\n");
    }

    status = pNtPowerInformation(ProcessorInformation, 0, 0, ppi, size);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);

    HeapFree(GetProcessHeap(), 0, ppi);
}

static void test_query_process_wow64(void)
{
    NTSTATUS status;
    ULONG ReturnLength;
    ULONG_PTR pbi[2], dummy;

    memset(&dummy, 0xcc, sizeof(dummy));

    /* Do not give a handle and buffer */
    status = pNtQueryInformationProcess(NULL, ProcessWow64Information, NULL, 0, NULL);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    /* Use a correct info class and buffer size, but still no handle and buffer */
    status = pNtQueryInformationProcess(NULL, ProcessWow64Information, NULL, sizeof(ULONG_PTR), NULL);
    ok( status == STATUS_ACCESS_VIOLATION || status == STATUS_INVALID_HANDLE,
        "Expected STATUS_ACCESS_VIOLATION or STATUS_INVALID_HANDLE, got %08x\n", status);

    /* Use a correct info class, buffer size and handle, but no buffer */
    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessWow64Information, NULL, sizeof(ULONG_PTR), NULL);
    ok( status == STATUS_ACCESS_VIOLATION , "Expected STATUS_ACCESS_VIOLATION, got %08x\n", status);

    /* Use a correct info class, buffer and buffer size, but no handle */
    pbi[0] = pbi[1] = dummy;
    status = pNtQueryInformationProcess(NULL, ProcessWow64Information, pbi, sizeof(ULONG_PTR), NULL);
    ok( status == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got %08x\n", status);
    ok( pbi[0] == dummy, "pbi[0] changed to %lx\n", pbi[0]);
    ok( pbi[1] == dummy, "pbi[1] changed to %lx\n", pbi[1]);

    /* Use a greater buffer size */
    pbi[0] = pbi[1] = dummy;
    status = pNtQueryInformationProcess(NULL, ProcessWow64Information, pbi, sizeof(ULONG_PTR) + 1, NULL);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);
    ok( pbi[0] == dummy, "pbi[0] changed to %lx\n", pbi[0]);
    ok( pbi[1] == dummy, "pbi[1] changed to %lx\n", pbi[1]);

    /* Use no ReturnLength */
    pbi[0] = pbi[1] = dummy;
    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessWow64Information, pbi, sizeof(ULONG_PTR), NULL);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( is_wow64 == (pbi[0] != 0), "is_wow64 %x, pbi[0] %lx\n", is_wow64, pbi[0]);
    ok( pbi[0] != dummy, "pbi[0] %lx\n", pbi[0]);
    ok( pbi[1] == dummy, "pbi[1] changed to %lx\n", pbi[1]);
    /* Test written size on 64 bit by checking high 32 bit buffer */
    if (sizeof(ULONG_PTR) > sizeof(DWORD))
    {
        DWORD *ptr = (DWORD *)pbi;
        ok( ptr[1] != (DWORD)dummy, "ptr[1] unchanged!\n");
    }

    /* Finally some correct calls */
    pbi[0] = pbi[1] = dummy;
    ReturnLength = 0xdeadbeef;
    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessWow64Information, pbi, sizeof(ULONG_PTR), &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( is_wow64 == (pbi[0] != 0), "is_wow64 %x, pbi[0] %lx\n", is_wow64, pbi[0]);
    ok( pbi[1] == dummy, "pbi[1] changed to %lx\n", pbi[1]);
    ok( ReturnLength == sizeof(ULONG_PTR), "Inconsistent length %d\n", ReturnLength);

    /* Everything is correct except a too small buffer size */
    pbi[0] = pbi[1] = dummy;
    ReturnLength = 0xdeadbeef;
    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessWow64Information, pbi, sizeof(ULONG_PTR) - 1, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);
    ok( pbi[0] == dummy, "pbi[0] changed to %lx\n", pbi[0]);
    ok( pbi[1] == dummy, "pbi[1] changed to %lx\n", pbi[1]);
    todo_wine ok( ReturnLength == 0xdeadbeef, "Expected 0xdeadbeef, got %d\n", ReturnLength);

    /* Everything is correct except a too large buffer size */
    pbi[0] = pbi[1] = dummy;
    ReturnLength = 0xdeadbeef;
    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessWow64Information, pbi, sizeof(ULONG_PTR) + 1, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);
    ok( pbi[0] == dummy, "pbi[0] changed to %lx\n", pbi[0]);
    ok( pbi[1] == dummy, "pbi[1] changed to %lx\n", pbi[1]);
    todo_wine ok( ReturnLength == 0xdeadbeef, "Expected 0xdeadbeef, got %d\n", ReturnLength);
}

static void test_query_process_basic(void)
{
    NTSTATUS status;
    ULONG ReturnLength;

    typedef struct _PROCESS_BASIC_INFORMATION_PRIVATE {
        DWORD_PTR ExitStatus;
        PPEB      PebBaseAddress;
        DWORD_PTR AffinityMask;
        DWORD_PTR BasePriority;
        ULONG_PTR UniqueProcessId;
        ULONG_PTR InheritedFromUniqueProcessId;
    } PROCESS_BASIC_INFORMATION_PRIVATE;

    PROCESS_BASIC_INFORMATION_PRIVATE pbi;

    /* This test also covers some basic parameter testing that should be the same for
     * every information class
    */

    status = pNtQueryInformationProcess(NULL, -1, NULL, 0, NULL);
    ok( status == STATUS_INVALID_INFO_CLASS || status == STATUS_NOT_IMPLEMENTED /* vista */,
        "Expected STATUS_INVALID_INFO_CLASS or STATUS_NOT_IMPLEMENTED, got %08x\n", status);

    status = pNtQueryInformationProcess(NULL, ProcessBasicInformation, NULL, 0, NULL);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    status = pNtQueryInformationProcess(NULL, ProcessBasicInformation, NULL, sizeof(pbi), NULL);
    ok( status == STATUS_ACCESS_VIOLATION || status == STATUS_INVALID_HANDLE,
        "Expected STATUS_ACCESS_VIOLATION or STATUS_INVALID_HANDLE(W2K3), got %08x\n", status);

    status = pNtQueryInformationProcess(NULL, ProcessBasicInformation, &pbi, sizeof(pbi), NULL);
    ok( status == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got %08x\n", status);

    status = pNtQueryInformationProcess(NULL, ProcessBasicInformation, &pbi, sizeof(pbi) * 2, NULL);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformation, &pbi, sizeof(pbi), NULL);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformation, &pbi, sizeof(pbi), &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( sizeof(pbi) == ReturnLength, "Inconsistent length %d\n", ReturnLength);

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformation, &pbi, sizeof(pbi) * 2, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);
    ok( sizeof(pbi) == ReturnLength, "Inconsistent length %d\n", ReturnLength);

    if (winetest_debug > 1) trace("ProcessID : %lx\n", pbi.UniqueProcessId);
    ok( pbi.UniqueProcessId > 0, "Expected a ProcessID > 0, got 0\n");
}

static void dump_vm_counters(const char *header, const VM_COUNTERS_EX *pvi)
{
    trace("%s:\n", header);
    trace("PeakVirtualSize           : %lu\n", pvi->PeakVirtualSize);
    trace("VirtualSize               : %lu\n", pvi->VirtualSize);
    trace("PageFaultCount            : %u\n",  pvi->PageFaultCount);
    trace("PeakWorkingSetSize        : %lu\n", pvi->PeakWorkingSetSize);
    trace("WorkingSetSize            : %lu\n", pvi->WorkingSetSize);
    trace("QuotaPeakPagedPoolUsage   : %lu\n", pvi->QuotaPeakPagedPoolUsage);
    trace("QuotaPagedPoolUsage       : %lu\n", pvi->QuotaPagedPoolUsage);
    trace("QuotaPeakNonPagePoolUsage : %lu\n", pvi->QuotaPeakNonPagedPoolUsage);
    trace("QuotaNonPagePoolUsage     : %lu\n", pvi->QuotaNonPagedPoolUsage);
    trace("PagefileUsage             : %lu\n", pvi->PagefileUsage);
    trace("PeakPagefileUsage         : %lu\n", pvi->PeakPagefileUsage);
}

static void test_query_process_vm(void)
{
    NTSTATUS status;
    ULONG ReturnLength;
    VM_COUNTERS_EX pvi;
    HANDLE process;
    SIZE_T prev_size;
    const SIZE_T alloc_size = 16 * 1024 * 1024;
    void *ptr;

    status = pNtQueryInformationProcess(NULL, ProcessVmCounters, NULL, sizeof(pvi), NULL);
    ok( status == STATUS_ACCESS_VIOLATION || status == STATUS_INVALID_HANDLE,
        "Expected STATUS_ACCESS_VIOLATION or STATUS_INVALID_HANDLE(W2K3), got %08x\n", status);

    status = pNtQueryInformationProcess(NULL, ProcessVmCounters, &pvi, sizeof(VM_COUNTERS), NULL);
    ok( status == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got %08x\n", status);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessVmCounters, &pvi, 24, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessVmCounters, &pvi, sizeof(VM_COUNTERS), &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( ReturnLength == sizeof(VM_COUNTERS), "Inconsistent length %d\n", ReturnLength);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessVmCounters, &pvi, 46, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);
    todo_wine ok( ReturnLength == sizeof(VM_COUNTERS), "wrong size %d\n", ReturnLength);

    /* Check if we have some return values */
    if (winetest_debug > 1)
        dump_vm_counters("VM counters for GetCurrentProcess", &pvi);
    ok( pvi.WorkingSetSize > 0, "Expected a WorkingSetSize > 0\n");
    ok( pvi.PagefileUsage > 0, "Expected a PagefileUsage > 0\n");

    process = OpenProcess(PROCESS_VM_READ, FALSE, GetCurrentProcessId());
    status = pNtQueryInformationProcess(process, ProcessVmCounters, &pvi, sizeof(pvi), NULL);
    ok( status == STATUS_ACCESS_DENIED, "Expected STATUS_ACCESS_DENIED, got %08x\n", status);
    CloseHandle(process);

    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    status = pNtQueryInformationProcess(process, ProcessVmCounters, &pvi, sizeof(pvi), NULL);
    ok( status == STATUS_SUCCESS || broken(!process) /* XP */, "Expected STATUS_SUCCESS, got %08x\n", status);
    CloseHandle(process);

    memset(&pvi, 0, sizeof(pvi));
    process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());
    status = pNtQueryInformationProcess(process, ProcessVmCounters, &pvi, sizeof(pvi), NULL);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( pvi.PrivateUsage == pvi.PagefileUsage, "wrong value %lu/%lu\n", pvi.PrivateUsage, pvi.PagefileUsage );

    /* Check if we have some return values */
    if (winetest_debug > 1)
        dump_vm_counters("VM counters for GetCurrentProcessId", &pvi);
    ok( pvi.WorkingSetSize > 0, "Expected a WorkingSetSize > 0\n");
    ok( pvi.PagefileUsage > 0, "Expected a PagefileUsage > 0\n");

    CloseHandle(process);

    /* Check if we have real counters */
    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessVmCounters, &pvi, sizeof(pvi), NULL);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( pvi.PrivateUsage == pvi.PagefileUsage, "wrong value %lu/%lu\n", pvi.PrivateUsage, pvi.PagefileUsage );
    prev_size = pvi.VirtualSize;
    if (winetest_debug > 1)
        dump_vm_counters("VM counters before VirtualAlloc", &pvi);
    ptr = VirtualAlloc(NULL, alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok( ptr != NULL, "VirtualAlloc failed, err %u\n", GetLastError());
    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessVmCounters, &pvi, sizeof(pvi), NULL);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( pvi.PrivateUsage == pvi.PagefileUsage, "wrong value %lu/%lu\n", pvi.PrivateUsage, pvi.PagefileUsage );
    if (winetest_debug > 1)
        dump_vm_counters("VM counters after VirtualAlloc", &pvi);
    todo_wine ok( pvi.VirtualSize >= prev_size + alloc_size,
        "Expected to be greater than %lu, got %lu\n", prev_size + alloc_size, pvi.VirtualSize);
    VirtualFree( ptr, 0, MEM_RELEASE);

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessVmCounters, &pvi, sizeof(pvi), NULL);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( pvi.PrivateUsage == pvi.PagefileUsage, "wrong value %lu/%lu\n", pvi.PrivateUsage, pvi.PagefileUsage );
    prev_size = pvi.VirtualSize;
    if (winetest_debug > 1)
        dump_vm_counters("VM counters before VirtualAlloc", &pvi);
    ptr = VirtualAlloc(NULL, alloc_size, MEM_RESERVE, PAGE_READWRITE);
    ok( ptr != NULL, "VirtualAlloc failed, err %u\n", GetLastError());
    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessVmCounters, &pvi, sizeof(pvi), NULL);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( pvi.PrivateUsage == pvi.PagefileUsage, "wrong value %lu/%lu\n", pvi.PrivateUsage, pvi.PagefileUsage );
    if (winetest_debug > 1)
        dump_vm_counters("VM counters after VirtualAlloc(MEM_RESERVE)", &pvi);
    todo_wine ok( pvi.VirtualSize >= prev_size + alloc_size,
        "Expected to be greater than %lu, got %lu\n", prev_size + alloc_size, pvi.VirtualSize);
    prev_size = pvi.VirtualSize;

    ptr = VirtualAlloc(ptr, alloc_size, MEM_COMMIT, PAGE_READWRITE);
    ok( ptr != NULL, "VirtualAlloc failed, err %u\n", GetLastError());
    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessVmCounters, &pvi, sizeof(pvi), NULL);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( pvi.PrivateUsage == pvi.PagefileUsage, "wrong value %lu/%lu\n", pvi.PrivateUsage, pvi.PagefileUsage );
    if (winetest_debug > 1)
        dump_vm_counters("VM counters after VirtualAlloc(MEM_COMMIT)", &pvi);
    ok( pvi.VirtualSize == prev_size,
        "Expected to equal to %lu, got %lu\n", prev_size, pvi.VirtualSize);
    VirtualFree( ptr, 0, MEM_RELEASE);
}

static void test_query_process_io(void)
{
    NTSTATUS status;
    ULONG ReturnLength;
    IO_COUNTERS pii;

    status = pNtQueryInformationProcess(NULL, ProcessIoCounters, NULL, sizeof(pii), NULL);
    ok( status == STATUS_ACCESS_VIOLATION || status == STATUS_INVALID_HANDLE,
        "Expected STATUS_ACCESS_VIOLATION or STATUS_INVALID_HANDLE(W2K3), got %08x\n", status);

    status = pNtQueryInformationProcess(NULL, ProcessIoCounters, &pii, sizeof(pii), NULL);
    ok( status == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got %08x\n", status);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessIoCounters, &pii, 24, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessIoCounters, &pii, sizeof(pii), &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( sizeof(pii) == ReturnLength, "Inconsistent length %d\n", ReturnLength);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessIoCounters, &pii, sizeof(pii) * 2, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);
    ok( sizeof(pii) == ReturnLength, "Inconsistent length %d\n", ReturnLength);

    /* Check if we have some return values */
    if (winetest_debug > 1) trace("OtherOperationCount : 0x%s\n", wine_dbgstr_longlong(pii.OtherOperationCount));
    todo_wine
    {
        ok( pii.OtherOperationCount > 0, "Expected an OtherOperationCount > 0\n");
    }
}

static void test_query_process_times(void)
{
    NTSTATUS status;
    ULONG ReturnLength;
    HANDLE process;
    SYSTEMTIME UTC, Local;
    KERNEL_USER_TIMES spti;

    status = pNtQueryInformationProcess(NULL, ProcessTimes, NULL, sizeof(spti), NULL);
    ok( status == STATUS_ACCESS_VIOLATION || status == STATUS_INVALID_HANDLE,
        "Expected STATUS_ACCESS_VIOLATION or STATUS_INVALID_HANDLE(W2K3), got %08x\n", status);

    status = pNtQueryInformationProcess(NULL, ProcessTimes, &spti, sizeof(spti), NULL);
    ok( status == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got %08x\n", status);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessTimes, &spti, 24, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, one_before_last_pid);
    if (!process)
    {
        if (winetest_debug > 1) trace("Could not open process with ID : %d, error : %u. Going to use current one.\n", one_before_last_pid, GetLastError());
        process = GetCurrentProcess();
    }
    else
        trace("ProcessTimes for process with ID : %d\n", one_before_last_pid);

    status = pNtQueryInformationProcess( process, ProcessTimes, &spti, sizeof(spti), &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( sizeof(spti) == ReturnLength, "Inconsistent length %d\n", ReturnLength);
    CloseHandle(process);

    FileTimeToSystemTime((const FILETIME *)&spti.CreateTime, &UTC);
    SystemTimeToTzSpecificLocalTime(NULL, &UTC, &Local);
    if (winetest_debug > 1) trace("CreateTime : %02d/%02d/%04d %02d:%02d:%02d\n", Local.wMonth, Local.wDay, Local.wYear,
           Local.wHour, Local.wMinute, Local.wSecond);

    FileTimeToSystemTime((const FILETIME *)&spti.ExitTime, &UTC);
    SystemTimeToTzSpecificLocalTime(NULL, &UTC, &Local);
    if (winetest_debug > 1) trace("ExitTime   : %02d/%02d/%04d %02d:%02d:%02d\n", Local.wMonth, Local.wDay, Local.wYear,
           Local.wHour, Local.wMinute, Local.wSecond);

    FileTimeToSystemTime((const FILETIME *)&spti.KernelTime, &Local);
    if (winetest_debug > 1) trace("KernelTime : %02d:%02d:%02d.%03d\n", Local.wHour, Local.wMinute, Local.wSecond, Local.wMilliseconds);

    FileTimeToSystemTime((const FILETIME *)&spti.UserTime, &Local);
    if (winetest_debug > 1) trace("UserTime   : %02d:%02d:%02d.%03d\n", Local.wHour, Local.wMinute, Local.wSecond, Local.wMilliseconds);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessTimes, &spti, sizeof(spti) * 2, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);
    ok( sizeof(spti) == ReturnLength ||
        ReturnLength == 0 /* vista */ ||
        broken(is_wow64),  /* returns garbage on wow64 */
        "Inconsistent length %d\n", ReturnLength);
}

static void test_query_process_debug_port(int argc, char **argv)
{
    DWORD_PTR debug_port = 0xdeadbeef;
    char cmdline[MAX_PATH];
    PROCESS_INFORMATION pi;
    STARTUPINFOA si = { 0 };
    NTSTATUS status;
    BOOL ret;

    sprintf(cmdline, "%s %s %s", argv[0], argv[1], "debuggee");

    si.cb = sizeof(si);
    ret = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, DEBUG_PROCESS, NULL, NULL, &si, &pi);
    ok(ret, "CreateProcess failed, last error %#x.\n", GetLastError());
    if (!ret) return;

    status = pNtQueryInformationProcess(NULL, ProcessDebugPort,
            NULL, 0, NULL);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %#x.\n", status);

    status = pNtQueryInformationProcess(NULL, ProcessDebugPort,
            NULL, sizeof(debug_port), NULL);
    ok(status == STATUS_INVALID_HANDLE || status == STATUS_ACCESS_VIOLATION /* XP */, "got %#x\n", status);

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessDebugPort,
            NULL, sizeof(debug_port), NULL);
    ok(status == STATUS_ACCESS_VIOLATION, "Expected STATUS_ACCESS_VIOLATION, got %#x.\n", status);

    status = pNtQueryInformationProcess(NULL, ProcessDebugPort,
            &debug_port, sizeof(debug_port), NULL);
    ok(status == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got %#x.\n", status);

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessDebugPort,
            &debug_port, sizeof(debug_port) - 1, NULL);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %#x.\n", status);

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessDebugPort,
            &debug_port, sizeof(debug_port) + 1, NULL);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %#x.\n", status);

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessDebugPort,
            &debug_port, sizeof(debug_port), NULL);
    ok(!status, "NtQueryInformationProcess failed, status %#x.\n", status);
    ok(debug_port == 0, "Expected port 0, got %#lx.\n", debug_port);

    status = pNtQueryInformationProcess(pi.hProcess, ProcessDebugPort,
            &debug_port, sizeof(debug_port), NULL);
    ok(!status, "NtQueryInformationProcess failed, status %#x.\n", status);
    ok(debug_port == ~(DWORD_PTR)0, "Expected port %#lx, got %#lx.\n", ~(DWORD_PTR)0, debug_port);

    for (;;)
    {
        DEBUG_EVENT ev;

        ret = WaitForDebugEvent(&ev, INFINITE);
        ok(ret, "WaitForDebugEvent failed, last error %#x.\n", GetLastError());
        if (!ret) break;

        if (ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) break;

        ret = ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
        ok(ret, "ContinueDebugEvent failed, last error %#x.\n", GetLastError());
        if (!ret) break;
    }

    ret = CloseHandle(pi.hThread);
    ok(ret, "CloseHandle failed, last error %#x.\n", GetLastError());
    ret = CloseHandle(pi.hProcess);
    ok(ret, "CloseHandle failed, last error %#x.\n", GetLastError());
}

static void test_query_process_priority(void)
{
    PROCESS_PRIORITY_CLASS priority[2];
    ULONG ReturnLength;
    DWORD orig_priority;
    NTSTATUS status;
    BOOL ret;

    status = pNtQueryInformationProcess(NULL, ProcessPriorityClass, NULL, sizeof(priority[0]), NULL);
    ok(status == STATUS_ACCESS_VIOLATION || broken(status == STATUS_INVALID_HANDLE) /* w2k3 */,
       "Expected STATUS_ACCESS_VIOLATION, got %08x\n", status);

    status = pNtQueryInformationProcess(NULL, ProcessPriorityClass, &priority, sizeof(priority[0]), NULL);
    ok(status == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got %08x\n", status);

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessPriorityClass, &priority, 1, &ReturnLength);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessPriorityClass, &priority, sizeof(priority), &ReturnLength);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    orig_priority = GetPriorityClass(GetCurrentProcess());
    ret = SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    ok(ret, "Failed to set priority class: %u\n", GetLastError());

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessPriorityClass, &priority, sizeof(priority[0]), &ReturnLength);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok(priority[0].PriorityClass == PROCESS_PRIOCLASS_BELOW_NORMAL,
       "Expected PROCESS_PRIOCLASS_BELOW_NORMAL, got %u\n", priority[0].PriorityClass);

    ret = SetPriorityClass(GetCurrentProcess(), orig_priority);
    ok(ret, "Failed to reset priority class: %u\n", GetLastError());
}

static void test_query_process_handlecount(void)
{
    NTSTATUS status;
    ULONG ReturnLength;
    DWORD handlecount;
    BYTE buffer[2 * sizeof(DWORD)];
    HANDLE process;

    status = pNtQueryInformationProcess(NULL, ProcessHandleCount, NULL, sizeof(handlecount), NULL);
    ok( status == STATUS_ACCESS_VIOLATION || status == STATUS_INVALID_HANDLE,
        "Expected STATUS_ACCESS_VIOLATION or STATUS_INVALID_HANDLE(W2K3), got %08x\n", status);

    status = pNtQueryInformationProcess(NULL, ProcessHandleCount, &handlecount, sizeof(handlecount), NULL);
    ok( status == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got %08x\n", status);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessHandleCount, &handlecount, 2, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, one_before_last_pid);
    if (!process)
    {
        trace("Could not open process with ID : %d, error : %u. Going to use current one.\n", one_before_last_pid, GetLastError());
        process = GetCurrentProcess();
    }
    else
        if (winetest_debug > 1) trace("ProcessHandleCount for process with ID : %d\n", one_before_last_pid);

    status = pNtQueryInformationProcess( process, ProcessHandleCount, &handlecount, sizeof(handlecount), &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( sizeof(handlecount) == ReturnLength, "Inconsistent length %d\n", ReturnLength);
    CloseHandle(process);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessHandleCount, buffer, sizeof(buffer), &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH || status == STATUS_SUCCESS,
        "Expected STATUS_INFO_LENGTH_MISMATCH or STATUS_SUCCESS, got %08x\n", status);
    ok( sizeof(handlecount) == ReturnLength, "Inconsistent length %d\n", ReturnLength);

    /* Check if we have some return values */
    if (winetest_debug > 1) trace("HandleCount : %d\n", handlecount);
    todo_wine
    {
        ok( handlecount > 0, "Expected some handles, got 0\n");
    }
}

static void test_query_process_image_file_name(void)
{
    static const WCHAR deviceW[] = {'\\','D','e','v','i','c','e','\\'};
    NTSTATUS status;
    ULONG ReturnLength;
    UNICODE_STRING image_file_name;
    UNICODE_STRING *buffer = NULL;

    status = pNtQueryInformationProcess(NULL, ProcessImageFileName, &image_file_name, sizeof(image_file_name), NULL);
    ok( status == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got %08x\n", status);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessImageFileName, &image_file_name, 2, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessImageFileName, &image_file_name, sizeof(image_file_name), &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    buffer = heap_alloc(ReturnLength);
    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessImageFileName, buffer, ReturnLength, &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
todo_wine
    ok(!memcmp(buffer->Buffer, deviceW, sizeof(deviceW)),
        "Expected image name to begin with \\Device\\, got %s\n",
        wine_dbgstr_wn(buffer->Buffer, buffer->Length / sizeof(WCHAR)));
    heap_free(buffer);

    status = pNtQueryInformationProcess(NULL, ProcessImageFileNameWin32, &image_file_name, sizeof(image_file_name), NULL);
    if (status == STATUS_INVALID_INFO_CLASS)
    {
        win_skip("ProcessImageFileNameWin32 is not supported\n");
        return;
    }
    ok( status == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got %08x\n", status);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessImageFileNameWin32, &image_file_name, 2, &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessImageFileNameWin32, &image_file_name, sizeof(image_file_name), &ReturnLength);
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    buffer = heap_alloc(ReturnLength);
    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessImageFileNameWin32, buffer, ReturnLength, &ReturnLength);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok(memcmp(buffer->Buffer, deviceW, sizeof(deviceW)),
        "Expected image name not to begin with \\Device\\, got %s\n",
        wine_dbgstr_wn(buffer->Buffer, buffer->Length / sizeof(WCHAR)));
    heap_free(buffer);
}

static void test_query_process_image_info(void)
{
    IMAGE_NT_HEADERS *nt = RtlImageNtHeader( NtCurrentTeb()->Peb->ImageBaseAddress );
    NTSTATUS status;
    SECTION_IMAGE_INFORMATION info;
    ULONG len;

    status = pNtQueryInformationProcess( NULL, ProcessImageInformation, &info, sizeof(info), &len );
    ok( status == STATUS_INVALID_HANDLE || broken(status == STATUS_INVALID_PARAMETER), /* winxp */
        "got %08x\n", status);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessImageInformation, &info, sizeof(info)-1, &len );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "got %08x\n", status);

    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessImageInformation, &info, sizeof(info)+1, &len );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "got %08x\n", status);

    memset( &info, 0xcc, sizeof(info) );
    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessImageInformation, &info, sizeof(info), &len );
    ok( status == STATUS_SUCCESS, "got %08x\n", status);
    ok( len == sizeof(info), "wrong len %u\n", len );

    ok( info.MajorSubsystemVersion == nt->OptionalHeader.MajorSubsystemVersion,
        "wrong major version %x/%x\n",
        info.MajorSubsystemVersion, nt->OptionalHeader.MajorSubsystemVersion );
    ok( info.MinorSubsystemVersion == nt->OptionalHeader.MinorSubsystemVersion,
        "wrong minor version %x/%x\n",
        info.MinorSubsystemVersion, nt->OptionalHeader.MinorSubsystemVersion );
    ok( info.MajorOperatingSystemVersion == nt->OptionalHeader.MajorOperatingSystemVersion ||
        broken( !info.MajorOperatingSystemVersion ),  /* <= win8 */
        "wrong major OS version %x/%x\n",
        info.MajorOperatingSystemVersion, nt->OptionalHeader.MajorOperatingSystemVersion );
    ok( info.MinorOperatingSystemVersion == nt->OptionalHeader.MinorOperatingSystemVersion,
        "wrong minor OS version %x/%x\n",
        info.MinorOperatingSystemVersion, nt->OptionalHeader.MinorOperatingSystemVersion );
}

static void test_query_process_debug_object_handle(int argc, char **argv)
{
    char cmdline[MAX_PATH];
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi;
    BOOL ret;
    HANDLE debug_object;
    NTSTATUS status;

    sprintf(cmdline, "%s %s %s", argv[0], argv[1], "debuggee");

    si.cb = sizeof(si);
    ret = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, DEBUG_PROCESS, NULL,
                        NULL, &si, &pi);
    ok(ret, "CreateProcess failed with last error %u\n", GetLastError());
    if (!ret) return;

    status = pNtQueryInformationProcess(NULL, ProcessDebugObjectHandle, NULL,
            0, NULL);
    ok(status == STATUS_INFO_LENGTH_MISMATCH,
       "Expected NtQueryInformationProcess to return STATUS_INFO_LENGTH_MISMATCH, got 0x%08x\n",
       status);

    status = pNtQueryInformationProcess(NULL, ProcessDebugObjectHandle, NULL,
            sizeof(debug_object), NULL);
    ok(status == STATUS_INVALID_HANDLE ||
       status == STATUS_ACCESS_VIOLATION, /* XP */
       "Expected NtQueryInformationProcess to return STATUS_INVALID_HANDLE, got 0x%08x\n", status);

    status = pNtQueryInformationProcess(GetCurrentProcess(),
            ProcessDebugObjectHandle, NULL, sizeof(debug_object), NULL);
    ok(status == STATUS_ACCESS_VIOLATION,
       "Expected NtQueryInformationProcess to return STATUS_ACCESS_VIOLATION, got 0x%08x\n", status);

    status = pNtQueryInformationProcess(NULL, ProcessDebugObjectHandle,
            &debug_object, sizeof(debug_object), NULL);
    ok(status == STATUS_INVALID_HANDLE,
       "Expected NtQueryInformationProcess to return STATUS_ACCESS_VIOLATION, got 0x%08x\n", status);

    status = pNtQueryInformationProcess(GetCurrentProcess(),
            ProcessDebugObjectHandle, &debug_object,
            sizeof(debug_object) - 1, NULL);
    ok(status == STATUS_INFO_LENGTH_MISMATCH,
       "Expected NtQueryInformationProcess to return STATUS_INFO_LENGTH_MISMATCH, got 0x%08x\n", status);

    status = pNtQueryInformationProcess(GetCurrentProcess(),
            ProcessDebugObjectHandle, &debug_object,
            sizeof(debug_object) + 1, NULL);
    ok(status == STATUS_INFO_LENGTH_MISMATCH,
       "Expected NtQueryInformationProcess to return STATUS_INFO_LENGTH_MISMATCH, got 0x%08x\n", status);

    debug_object = (HANDLE)0xdeadbeef;
    status = pNtQueryInformationProcess(GetCurrentProcess(),
            ProcessDebugObjectHandle, &debug_object,
            sizeof(debug_object), NULL);
    ok(status == STATUS_PORT_NOT_SET,
       "Expected NtQueryInformationProcess to return STATUS_PORT_NOT_SET, got 0x%08x\n", status);
    ok(debug_object == NULL ||
       broken(debug_object == (HANDLE)0xdeadbeef), /* Wow64 */
       "Expected debug object handle to be NULL, got %p\n", debug_object);

    debug_object = (HANDLE)0xdeadbeef;
    status = pNtQueryInformationProcess(pi.hProcess, ProcessDebugObjectHandle,
            &debug_object, sizeof(debug_object), NULL);
    ok(status == STATUS_SUCCESS,
       "Expected NtQueryInformationProcess to return STATUS_SUCCESS, got 0x%08x\n", status);
    ok(debug_object != NULL,
       "Expected debug object handle to be non-NULL, got %p\n", debug_object);
    status = NtClose( debug_object );
    ok( !status, "NtClose failed %x\n", status );

    for (;;)
    {
        DEBUG_EVENT ev;

        ret = WaitForDebugEvent(&ev, INFINITE);
        ok(ret, "WaitForDebugEvent failed with last error %u\n", GetLastError());
        if (!ret) break;

        if (ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) break;

        ret = ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
        ok(ret, "ContinueDebugEvent failed with last error %u\n", GetLastError());
        if (!ret) break;
    }

    ret = CloseHandle(pi.hThread);
    ok(ret, "CloseHandle failed with last error %u\n", GetLastError());
    ret = CloseHandle(pi.hProcess);
    ok(ret, "CloseHandle failed with last error %u\n", GetLastError());
}

static void test_query_process_debug_flags(int argc, char **argv)
{
    static const DWORD test_flags[] = { DEBUG_PROCESS,
                                        DEBUG_ONLY_THIS_PROCESS,
                                        DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS,
                                        CREATE_SUSPENDED };
    DWORD debug_flags = 0xdeadbeef;
    char cmdline[MAX_PATH];
    PROCESS_INFORMATION pi;
    STARTUPINFOA si = { 0 };
    NTSTATUS status;
    DEBUG_EVENT ev;
    DWORD result;
    BOOL ret;
    int i, j;

    /* test invalid arguments */
    status = pNtQueryInformationProcess(NULL, ProcessDebugFlags, NULL, 0, NULL);
    ok(status == STATUS_INFO_LENGTH_MISMATCH || broken(status == STATUS_INVALID_INFO_CLASS) /* WOW64 */,
            "Expected STATUS_INFO_LENGTH_MISMATCH, got %#x.\n", status);

    status = pNtQueryInformationProcess(NULL, ProcessDebugFlags, NULL, sizeof(debug_flags), NULL);
    ok(status == STATUS_INVALID_HANDLE || status == STATUS_ACCESS_VIOLATION || broken(status == STATUS_INVALID_INFO_CLASS) /* WOW64 */,
            "Expected STATUS_INVALID_HANDLE, got %#x.\n", status);

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessDebugFlags,
            NULL, sizeof(debug_flags), NULL);
    ok(status == STATUS_ACCESS_VIOLATION, "Expected STATUS_ACCESS_VIOLATION, got %#x.\n", status);

    status = pNtQueryInformationProcess(NULL, ProcessDebugFlags,
            &debug_flags, sizeof(debug_flags), NULL);
    ok(status == STATUS_INVALID_HANDLE || broken(status == STATUS_INVALID_INFO_CLASS) /* WOW64 */,
            "Expected STATUS_INVALID_HANDLE, got %#x.\n", status);

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessDebugFlags,
            &debug_flags, sizeof(debug_flags) - 1, NULL);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %#x.\n", status);

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessDebugFlags,
            &debug_flags, sizeof(debug_flags) + 1, NULL);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %#x.\n", status);

    /* test ProcessDebugFlags of current process */
    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessDebugFlags,
            &debug_flags, sizeof(debug_flags), NULL);
    ok(!status, "NtQueryInformationProcess failed, status %#x.\n", status);
    ok(debug_flags == TRUE, "Expected flag TRUE, got %x.\n", debug_flags);

    for (i = 0; i < ARRAY_SIZE(test_flags); i++)
    {
        DWORD expected_flags = !(test_flags[i] & DEBUG_ONLY_THIS_PROCESS);
        sprintf(cmdline, "%s %s %s", argv[0], argv[1], "debuggee");

        si.cb = sizeof(si);
        ret = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, test_flags[i], NULL, NULL, &si, &pi);
        ok(ret, "CreateProcess failed, last error %#x.\n", GetLastError());

        if (!(test_flags[i] & (DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS)))
        {
            /* test ProcessDebugFlags before attaching with debugger */
            status = pNtQueryInformationProcess(pi.hProcess, ProcessDebugFlags,
                    &debug_flags, sizeof(debug_flags), NULL);
            ok(!status, "NtQueryInformationProcess failed, status %#x.\n", status);
            ok(debug_flags == TRUE, "Expected flag TRUE, got %x.\n", debug_flags);

            ret = DebugActiveProcess(pi.dwProcessId);
            ok(ret, "DebugActiveProcess failed, last error %#x.\n", GetLastError());
            expected_flags = FALSE;
        }

        /* test ProcessDebugFlags after attaching with debugger */
        status = pNtQueryInformationProcess(pi.hProcess, ProcessDebugFlags,
                &debug_flags, sizeof(debug_flags), NULL);
        ok(!status, "NtQueryInformationProcess failed, status %#x.\n", status);
        ok(debug_flags == expected_flags, "Expected flag %x, got %x.\n", expected_flags, debug_flags);

        if (!(test_flags[i] & CREATE_SUSPENDED))
        {
            /* Continue a couple of times to make sure the process is fully initialized,
             * otherwise Windows XP deadlocks in the following DebugActiveProcess(). */
            for (;;)
            {
                ret = WaitForDebugEvent(&ev, 1000);
                ok(ret, "WaitForDebugEvent failed, last error %#x.\n", GetLastError());
                if (!ret) break;

                if (ev.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) break;

                ret = ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
                ok(ret, "ContinueDebugEvent failed, last error %#x.\n", GetLastError());
                if (!ret) break;
            }

            result = SuspendThread(pi.hThread);
            ok(result == 0, "Expected 0, got %u.\n", result);
        }

        ret = DebugActiveProcessStop(pi.dwProcessId);
        ok(ret, "DebugActiveProcessStop failed, last error %#x.\n", GetLastError());

        /* test ProcessDebugFlags after detaching debugger */
        status = pNtQueryInformationProcess(pi.hProcess, ProcessDebugFlags,
                &debug_flags, sizeof(debug_flags), NULL);
        ok(!status, "NtQueryInformationProcess failed, status %#x.\n", status);
        ok(debug_flags == expected_flags, "Expected flag %x, got %x.\n", expected_flags, debug_flags);

        ret = DebugActiveProcess(pi.dwProcessId);
        ok(ret, "DebugActiveProcess failed, last error %#x.\n", GetLastError());

        /* test ProcessDebugFlags after re-attaching debugger */
        status = pNtQueryInformationProcess(pi.hProcess, ProcessDebugFlags,
                &debug_flags, sizeof(debug_flags), NULL);
        ok(!status, "NtQueryInformationProcess failed, status %#x.\n", status);
        ok(debug_flags == FALSE, "Expected flag FALSE, got %x.\n", debug_flags);

        result = ResumeThread(pi.hThread);
        todo_wine ok(result == 2, "Expected 2, got %u.\n", result);

        /* Wait until the process is terminated. On Windows XP the process randomly
         * gets stuck in a non-continuable exception, so stop after 100 iterations.
         * On Windows 2003, the debugged process disappears (or stops?) without
         * any EXIT_PROCESS_DEBUG_EVENT after a couple of events. */
        for (j = 0; j < 100; j++)
        {
            ret = WaitForDebugEvent(&ev, 1000);
            ok(ret || broken(GetLastError() == ERROR_SEM_TIMEOUT),
                "WaitForDebugEvent failed, last error %#x.\n", GetLastError());
            if (!ret) break;

            if (ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) break;

            ret = ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
            ok(ret, "ContinueDebugEvent failed, last error %#x.\n", GetLastError());
            if (!ret) break;
        }
        ok(j < 100 || broken(j >= 100) /* Win XP */, "Expected less than 100 debug events.\n");

        /* test ProcessDebugFlags after process has terminated */
        status = pNtQueryInformationProcess(pi.hProcess, ProcessDebugFlags,
                &debug_flags, sizeof(debug_flags), NULL);
        ok(!status, "NtQueryInformationProcess failed, status %#x.\n", status);
        ok(debug_flags == FALSE, "Expected flag FALSE, got %x.\n", debug_flags);

        ret = CloseHandle(pi.hThread);
        ok(ret, "CloseHandle failed, last error %#x.\n", GetLastError());
        ret = CloseHandle(pi.hProcess);
        ok(ret, "CloseHandle failed, last error %#x.\n", GetLastError());
    }
}

static void test_readvirtualmemory(void)
{
    HANDLE process;
    NTSTATUS status;
    SIZE_T readcount;
    static const char teststring[] = "test string";
    char buffer[12];

    process = OpenProcess(PROCESS_VM_READ, FALSE, GetCurrentProcessId());
    ok(process != 0, "Expected to be able to open own process for reading memory\n");

    /* normal operation */
    status = pNtReadVirtualMemory(process, teststring, buffer, 12, &readcount);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( readcount == 12, "Expected to read 12 bytes, got %ld\n",readcount);
    ok( strcmp(teststring, buffer) == 0, "Expected read memory to be the same as original memory\n");

    /* no number of bytes */
    memset(buffer, 0, 12);
    status = pNtReadVirtualMemory(process, teststring, buffer, 12, NULL);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( strcmp(teststring, buffer) == 0, "Expected read memory to be the same as original memory\n");

    /* illegal remote address */
    todo_wine{
    status = pNtReadVirtualMemory(process, (void *) 0x1234, buffer, 12, &readcount);
    ok( status == STATUS_PARTIAL_COPY, "Expected STATUS_PARTIAL_COPY, got %08x\n", status);
    if (status == STATUS_PARTIAL_COPY)
        ok( readcount == 0, "Expected to read 0 bytes, got %ld\n",readcount);
    }

    /* 0 handle */
    status = pNtReadVirtualMemory(0, teststring, buffer, 12, &readcount);
    ok( status == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got %08x\n", status);
    ok( readcount == 0, "Expected to read 0 bytes, got %ld\n",readcount);

    /* pseudo handle for current process*/
    memset(buffer, 0, 12);
    status = pNtReadVirtualMemory((HANDLE)-1, teststring, buffer, 12, &readcount);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( readcount == 12, "Expected to read 12 bytes, got %ld\n",readcount);
    ok( strcmp(teststring, buffer) == 0, "Expected read memory to be the same as original memory\n");

    /* illegal local address */
    status = pNtReadVirtualMemory(process, teststring, (void *)0x1234, 12, &readcount);
    ok( status == STATUS_ACCESS_VIOLATION || broken(status == STATUS_PARTIAL_COPY) /* Win10 */,
        "Expected STATUS_ACCESS_VIOLATION, got %08x\n", status);
    if (status == STATUS_ACCESS_VIOLATION)
        ok( readcount == 0, "Expected to read 0 bytes, got %ld\n",readcount);

    CloseHandle(process);
}

static void test_mapprotection(void)
{
    HANDLE h;
    void* addr;
    MEMORY_BASIC_INFORMATION info;
    ULONG oldflags, flagsize, flags = MEM_EXECUTE_OPTION_ENABLE;
    LARGE_INTEGER size, offset;
    NTSTATUS status;
    SIZE_T retlen, count;
    void (*f)(void);
    BOOL reset_flags = FALSE;

    /* Switch to being a noexec unaware process */
    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessExecuteFlags, &oldflags, sizeof (oldflags), &flagsize);
    if (status == STATUS_INVALID_PARAMETER)
    {
        skip("Unable to query process execute flags on this platform\n");
        return;
    }
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status );
    if (winetest_debug > 1) trace("Process execute flags %08x\n", oldflags);

    if (!(oldflags & MEM_EXECUTE_OPTION_ENABLE))
    {
        if (oldflags & MEM_EXECUTE_OPTION_PERMANENT)
        {
            skip("Unable to turn off noexec\n");
            return;
        }

        if (pGetSystemDEPPolicy && pGetSystemDEPPolicy() == AlwaysOn)
        {
            skip("System policy requires noexec\n");
            return;
        }

        status = pNtSetInformationProcess( GetCurrentProcess(), ProcessExecuteFlags, &flags, sizeof(flags) );
        ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status );
        reset_flags = TRUE;
    }

    size.u.LowPart  = 0x2000;
    size.u.HighPart = 0;
    status = pNtCreateSection ( &h,
        STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ | SECTION_MAP_WRITE | SECTION_MAP_EXECUTE,
        NULL,
        &size,
        PAGE_READWRITE,
        SEC_COMMIT | SEC_NOCACHE,
        0
    );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);

    offset.u.LowPart  = 0;
    offset.u.HighPart = 0;
    count = 0x2000;
    addr = NULL;
    status = pNtMapViewOfSection ( h, GetCurrentProcess(), &addr, 0, 0, &offset, &count, ViewShare, 0, PAGE_READWRITE);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);

#if defined(__x86_64__) || defined(__i386__)
    *(unsigned char*)addr = 0xc3;       /* lret ... in both i386 and x86_64 */
#elif defined(__arm__)
    *(unsigned long*)addr = 0xe12fff1e; /* bx lr */
#elif defined(__aarch64__)
    *(unsigned long*)addr = 0xd65f03c0; /* ret */
#else
    ok(0, "Add a return opcode for your architecture or expect a crash in this test\n");
#endif
    if (winetest_debug > 1) trace("trying to execute code in the readwrite only mapped anon file...\n");
    f = addr;f();
    if (winetest_debug > 1) trace("...done.\n");

    status = pNtQueryVirtualMemory( GetCurrentProcess(), addr, MemoryBasicInformation, &info, sizeof(info), &retlen );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( retlen == sizeof(info), "Expected STATUS_SUCCESS, got %08x\n", status);
    ok((info.Protect & ~PAGE_NOCACHE) == PAGE_READWRITE, "addr.Protect is not PAGE_READWRITE, but 0x%x\n", info.Protect);

    status = pNtUnmapViewOfSection( GetCurrentProcess(), (char *)addr + 0x1050 );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    pNtClose (h);

    if (reset_flags)
        pNtSetInformationProcess( GetCurrentProcess(), ProcessExecuteFlags, &oldflags, sizeof(oldflags) );
}

static void test_threadstack(void)
{
    PROCESS_STACK_ALLOCATION_INFORMATION info = { 0x100000, 0, (void *)0xdeadbeef };
    PROCESS_STACK_ALLOCATION_INFORMATION_EX info_ex = { 0 };
    MEMORY_BASIC_INFORMATION meminfo;
    SIZE_T retlen;
    NTSTATUS status;

    info.ReserveSize = 0x100000;
    info.StackBase = (void *)0xdeadbeef;
    status = pNtSetInformationProcess( GetCurrentProcess(), ProcessThreadStackAllocation, &info, sizeof(info) );
    ok( !status, "NtSetInformationProcess failed %08x\n", status );
    ok( info.StackBase != (void *)0xdeadbeef, "stackbase not set\n" );

    status = pNtQueryVirtualMemory( GetCurrentProcess(), info.StackBase, MemoryBasicInformation,
                                    &meminfo, sizeof(meminfo), &retlen );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( retlen == sizeof(meminfo), "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( meminfo.AllocationBase == info.StackBase, "wrong base %p/%p\n",
        meminfo.AllocationBase, info.StackBase );
    ok( meminfo.RegionSize == info.ReserveSize, "wrong size %lx/%lx\n",
        meminfo.RegionSize, info.ReserveSize );
    ok( meminfo.State == MEM_RESERVE, "wrong state %x\n", meminfo.State );
    ok( meminfo.Protect == 0, "wrong protect %x\n", meminfo.Protect );
    ok( meminfo.Type == MEM_PRIVATE, "wrong type %x\n", meminfo.Type );

    info_ex.AllocInfo = info;
    status = pNtSetInformationProcess( GetCurrentProcess(), ProcessThreadStackAllocation,
                                       &info_ex, sizeof(info_ex) );
    if (status != STATUS_INVALID_PARAMETER)
    {
        ok( !status, "NtSetInformationProcess failed %08x\n", status );
        ok( info_ex.AllocInfo.StackBase != info.StackBase, "stackbase not set\n" );
        status = pNtQueryVirtualMemory( GetCurrentProcess(), info_ex.AllocInfo.StackBase,
                                        MemoryBasicInformation, &meminfo, sizeof(meminfo), &retlen );
        ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
        ok( retlen == sizeof(meminfo), "Expected STATUS_SUCCESS, got %08x\n", status);
        ok( meminfo.AllocationBase == info_ex.AllocInfo.StackBase, "wrong base %p/%p\n",
            meminfo.AllocationBase, info_ex.AllocInfo.StackBase );
        ok( meminfo.RegionSize == info_ex.AllocInfo.ReserveSize, "wrong size %lx/%lx\n",
            meminfo.RegionSize, info_ex.AllocInfo.ReserveSize );
        ok( meminfo.State == MEM_RESERVE, "wrong state %x\n", meminfo.State );
        ok( meminfo.Protect == 0, "wrong protect %x\n", meminfo.Protect );
        ok( meminfo.Type == MEM_PRIVATE, "wrong type %x\n", meminfo.Type );
        VirtualFree( info_ex.AllocInfo.StackBase, 0, MEM_FREE );
        status = pNtSetInformationProcess( GetCurrentProcess(), ProcessThreadStackAllocation,
                                           &info, sizeof(info) - 1 );
        ok( status == STATUS_INFO_LENGTH_MISMATCH, "NtSetInformationProcess failed %08x\n", status );
        status = pNtSetInformationProcess( GetCurrentProcess(), ProcessThreadStackAllocation,
                                           &info, sizeof(info) + 1 );
        ok( status == STATUS_INFO_LENGTH_MISMATCH, "NtSetInformationProcess failed %08x\n", status );
        status = pNtSetInformationProcess( GetCurrentProcess(), ProcessThreadStackAllocation,
                                           &info_ex, sizeof(info_ex) - 1 );
        ok( status == STATUS_INFO_LENGTH_MISMATCH, "NtSetInformationProcess failed %08x\n", status );
        status = pNtSetInformationProcess( GetCurrentProcess(), ProcessThreadStackAllocation,
                                           &info_ex, sizeof(info_ex) + 1 );
        ok( status == STATUS_INFO_LENGTH_MISMATCH, "NtSetInformationProcess failed %08x\n", status );
    }
    else win_skip( "ProcessThreadStackAllocation ex not supported\n" );

    VirtualFree( info.StackBase, 0, MEM_FREE );
}

static void test_queryvirtualmemory(void)
{
    NTSTATUS status;
    SIZE_T readcount, prev;
    static const char teststring[] = "test string";
    static char datatestbuf[42] = "abc";
    static char rwtestbuf[42];
    MEMORY_BASIC_INFORMATION mbi;
    char stackbuf[42];
    HMODULE module;
    void *user_shared_data = (void *)0x7ffe0000;
    char buffer[1024];
    MEMORY_SECTION_NAME *name = (MEMORY_SECTION_NAME *)buffer;

    module = GetModuleHandleA( "ntdll.dll" );
    status = pNtQueryVirtualMemory(NtCurrentProcess(), module, MemoryBasicInformation, &mbi, sizeof(MEMORY_BASIC_INFORMATION), &readcount);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( readcount == sizeof(MEMORY_BASIC_INFORMATION), "Expected to read %d bytes, got %ld\n",(int)sizeof(MEMORY_BASIC_INFORMATION),readcount);
    ok (mbi.AllocationBase == module, "mbi.AllocationBase is 0x%p, expected 0x%p\n", mbi.AllocationBase, module);
    ok (mbi.AllocationProtect == PAGE_EXECUTE_WRITECOPY, "mbi.AllocationProtect is 0x%x, expected 0x%x\n", mbi.AllocationProtect, PAGE_EXECUTE_WRITECOPY);
    ok (mbi.State == MEM_COMMIT, "mbi.State is 0x%x, expected 0x%x\n", mbi.State, MEM_COMMIT);
    ok (mbi.Protect == PAGE_READONLY, "mbi.Protect is 0x%x, expected 0x%x\n", mbi.Protect, PAGE_READONLY);
    ok (mbi.Type == MEM_IMAGE, "mbi.Type is 0x%x, expected 0x%x\n", mbi.Type, MEM_IMAGE);

    module = GetModuleHandleA( "ntdll.dll" );
    status = pNtQueryVirtualMemory(NtCurrentProcess(), pNtQueryVirtualMemory, MemoryBasicInformation, &mbi, sizeof(MEMORY_BASIC_INFORMATION), &readcount);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( readcount == sizeof(MEMORY_BASIC_INFORMATION), "Expected to read %d bytes, got %ld\n",(int)sizeof(MEMORY_BASIC_INFORMATION),readcount);
    ok (mbi.AllocationBase == module, "mbi.AllocationBase is 0x%p, expected 0x%p\n", mbi.AllocationBase, module);
    ok (mbi.AllocationProtect == PAGE_EXECUTE_WRITECOPY, "mbi.AllocationProtect is 0x%x, expected 0x%x\n", mbi.AllocationProtect, PAGE_EXECUTE_WRITECOPY);
    ok (mbi.State == MEM_COMMIT, "mbi.State is 0x%x, expected 0x%x\n", mbi.State, MEM_COMMIT);
    ok (mbi.Protect == PAGE_EXECUTE_READ, "mbi.Protect is 0x%x, expected 0x%x\n", mbi.Protect, PAGE_EXECUTE_READ);

    status = pNtQueryVirtualMemory(NtCurrentProcess(), GetProcessHeap(), MemoryBasicInformation, &mbi, sizeof(MEMORY_BASIC_INFORMATION), &readcount);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( readcount == sizeof(MEMORY_BASIC_INFORMATION), "Expected to read %d bytes, got %ld\n",(int)sizeof(MEMORY_BASIC_INFORMATION),readcount);
    ok (mbi.AllocationProtect == PAGE_READWRITE || mbi.AllocationProtect == PAGE_EXECUTE_READWRITE,
        "mbi.AllocationProtect is 0x%x\n", mbi.AllocationProtect);
    ok (mbi.State == MEM_COMMIT, "mbi.State is 0x%x, expected 0x%x\n", mbi.State, MEM_COMMIT);
    ok (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE,
        "mbi.Protect is 0x%x\n", mbi.Protect);

    status = pNtQueryVirtualMemory(NtCurrentProcess(), stackbuf, MemoryBasicInformation, &mbi, sizeof(MEMORY_BASIC_INFORMATION), &readcount);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( readcount == sizeof(MEMORY_BASIC_INFORMATION), "Expected to read %d bytes, got %ld\n",(int)sizeof(MEMORY_BASIC_INFORMATION),readcount);
    ok (mbi.AllocationProtect == PAGE_READWRITE, "mbi.AllocationProtect is 0x%x, expected 0x%x\n", mbi.AllocationProtect, PAGE_READWRITE);
    ok (mbi.State == MEM_COMMIT, "mbi.State is 0x%x, expected 0x%x\n", mbi.State, MEM_COMMIT);
    ok (mbi.Protect == PAGE_READWRITE, "mbi.Protect is 0x%x, expected 0x%x\n", mbi.Protect, PAGE_READWRITE);

    module = GetModuleHandleA( NULL );
    status = pNtQueryVirtualMemory(NtCurrentProcess(), teststring, MemoryBasicInformation, &mbi, sizeof(MEMORY_BASIC_INFORMATION), &readcount);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( readcount == sizeof(MEMORY_BASIC_INFORMATION), "Expected to read %d bytes, got %ld\n",(int)sizeof(MEMORY_BASIC_INFORMATION),readcount);
    ok (mbi.AllocationBase == module, "mbi.AllocationBase is 0x%p, expected 0x%p\n", mbi.AllocationBase, module);
    ok (mbi.AllocationProtect == PAGE_EXECUTE_WRITECOPY, "mbi.AllocationProtect is 0x%x, expected 0x%x\n", mbi.AllocationProtect, PAGE_EXECUTE_WRITECOPY);
    ok (mbi.State == MEM_COMMIT, "mbi.State is 0x%x, expected 0x%X\n", mbi.State, MEM_COMMIT);
    ok (mbi.Protect == PAGE_READONLY, "mbi.Protect is 0x%x, expected 0x%X\n", mbi.Protect, PAGE_READONLY);

    status = pNtQueryVirtualMemory(NtCurrentProcess(), datatestbuf, MemoryBasicInformation, &mbi, sizeof(MEMORY_BASIC_INFORMATION), &readcount);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( readcount == sizeof(MEMORY_BASIC_INFORMATION), "Expected to read %d bytes, got %ld\n",(int)sizeof(MEMORY_BASIC_INFORMATION),readcount);
    ok (mbi.AllocationBase == module, "mbi.AllocationBase is 0x%p, expected 0x%p\n", mbi.AllocationBase, module);
    ok (mbi.AllocationProtect == PAGE_EXECUTE_WRITECOPY, "mbi.AllocationProtect is 0x%x, expected 0x%x\n", mbi.AllocationProtect, PAGE_EXECUTE_WRITECOPY);
    ok (mbi.State == MEM_COMMIT, "mbi.State is 0x%x, expected 0x%X\n", mbi.State, MEM_COMMIT);
    ok (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY,
        "mbi.Protect is 0x%x\n", mbi.Protect);

    status = pNtQueryVirtualMemory(NtCurrentProcess(), rwtestbuf, MemoryBasicInformation, &mbi, sizeof(MEMORY_BASIC_INFORMATION), &readcount);
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( readcount == sizeof(MEMORY_BASIC_INFORMATION), "Expected to read %d bytes, got %ld\n",(int)sizeof(MEMORY_BASIC_INFORMATION),readcount);
    if (mbi.AllocationBase == module)
    {
        ok (mbi.AllocationProtect == PAGE_EXECUTE_WRITECOPY, "mbi.AllocationProtect is 0x%x, expected 0x%x\n", mbi.AllocationProtect, PAGE_EXECUTE_WRITECOPY);
        ok (mbi.State == MEM_COMMIT, "mbi.State is 0x%x, expected 0x%X\n", mbi.State, MEM_COMMIT);
        ok (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY,
            "mbi.Protect is 0x%x\n", mbi.Protect);
    }
    else skip( "bss is outside of module\n" );  /* this can happen on Mac OS */

    status = pNtQueryVirtualMemory(NtCurrentProcess(), user_shared_data, MemoryBasicInformation, &mbi, sizeof(MEMORY_BASIC_INFORMATION), &readcount);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok(readcount == sizeof(MEMORY_BASIC_INFORMATION), "Expected to read %d bytes, got %ld\n",(int)sizeof(MEMORY_BASIC_INFORMATION),readcount);
    ok(mbi.AllocationBase == user_shared_data, "mbi.AllocationBase is 0x%p, expected 0x%p\n", mbi.AllocationBase, user_shared_data);
    ok(mbi.AllocationProtect == PAGE_READONLY, "mbi.AllocationProtect is 0x%x, expected 0x%x\n", mbi.AllocationProtect, PAGE_READONLY);
    ok(mbi.State == MEM_COMMIT, "mbi.State is 0x%x, expected 0x%X\n", mbi.State, MEM_COMMIT);
    ok(mbi.Protect == PAGE_READONLY, "mbi.Protect is 0x%x\n", mbi.Protect);
    ok(mbi.Type == MEM_PRIVATE, "mbi.Type is 0x%x, expected 0x%x\n", mbi.Type, MEM_PRIVATE);
    ok(mbi.RegionSize == 0x1000, "mbi.RegionSize is 0x%lx, expected 0x%x\n", mbi.RegionSize, 0x1000);

    /* check error code when addr is higher than working set limit */
    status = pNtQueryVirtualMemory(NtCurrentProcess(), (void *)~0, MemoryBasicInformation, &mbi, sizeof(mbi), &readcount);
    ok(status == STATUS_INVALID_PARAMETER, "Expected STATUS_INVALID_PARAMETER, got %08x\n", status);
    /* check error code when len is less than MEMORY_BASIC_INFORMATION size */
    status = pNtQueryVirtualMemory(NtCurrentProcess(), GetProcessHeap(), MemoryBasicInformation, &mbi, sizeof(MEMORY_BASIC_INFORMATION) - 1, &readcount);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status);

    module = GetModuleHandleA( "ntdll.dll" );
    memset(buffer, 0xcc, sizeof(buffer));
    readcount = 0xdeadbeef;
    status = pNtQueryVirtualMemory(NtCurrentProcess(), module, MemorySectionName,
                                   name, sizeof(*name) + 16, &readcount);
    ok(status == STATUS_BUFFER_OVERFLOW, "got %08x\n", status);
    ok(name->SectionFileName.Length == 0xcccc || broken(!name->SectionFileName.Length),  /* vista64 */
       "Wrong len %u\n", name->SectionFileName.Length);
    ok(readcount > sizeof(*name), "Wrong count %lu\n", readcount);

    memset(buffer, 0xcc, sizeof(buffer));
    readcount = 0xdeadbeef;
    status = pNtQueryVirtualMemory(NtCurrentProcess(), (char *)module + 1234, MemorySectionName,
                                   name, sizeof(buffer), &readcount);
    ok(status == STATUS_SUCCESS, "got %08x\n", status);
    ok(name->SectionFileName.Buffer == (WCHAR *)(name + 1), "Wrong ptr %p/%p\n",
       name->SectionFileName.Buffer, name + 1 );
    ok(name->SectionFileName.Length != 0xcccc, "Wrong len %u\n", name->SectionFileName.Length);
    ok(name->SectionFileName.MaximumLength == name->SectionFileName.Length + sizeof(WCHAR),
       "Wrong maxlen %u/%u\n", name->SectionFileName.MaximumLength, name->SectionFileName.Length);
    ok(readcount == sizeof(name->SectionFileName) + name->SectionFileName.MaximumLength,
       "Wrong count %lu/%u\n", readcount, name->SectionFileName.MaximumLength);
    ok( !name->SectionFileName.Buffer[name->SectionFileName.Length / sizeof(WCHAR)],
        "buffer not null-terminated\n" );

    memset(buffer, 0xcc, sizeof(buffer));
    status = pNtQueryVirtualMemory(NtCurrentProcess(), (char *)module + 1234, MemorySectionName,
                                   name, sizeof(buffer), NULL);
    ok(status == STATUS_SUCCESS, "got %08x\n", status);

    status = pNtQueryVirtualMemory(NtCurrentProcess(), (char *)module + 1234, MemorySectionName,
                                   NULL, sizeof(buffer), NULL);
    ok(status == STATUS_ACCESS_VIOLATION, "got %08x\n", status);

    memset(buffer, 0xcc, sizeof(buffer));
    prev = readcount;
    readcount = 0xdeadbeef;
    status = pNtQueryVirtualMemory(NtCurrentProcess(), (char *)module + 321, MemorySectionName,
                                   name, sizeof(*name) - 1, &readcount);
    ok(status == STATUS_INFO_LENGTH_MISMATCH, "got %08x\n", status);
    ok(name->SectionFileName.Length == 0xcccc, "Wrong len %u\n", name->SectionFileName.Length);
    ok(readcount == prev, "Wrong count %lu\n", readcount);

    memset(buffer, 0xcc, sizeof(buffer));
    readcount = 0xdeadbeef;
    status = pNtQueryVirtualMemory((HANDLE)0xdead, (char *)module + 1234, MemorySectionName,
                                   name, sizeof(buffer), &readcount);
    ok(status == STATUS_INVALID_HANDLE, "got %08x\n", status);
    ok(readcount == 0xdeadbeef || broken(readcount == 1024 + sizeof(*name)), /* wow64 */
       "Wrong count %lu\n", readcount);

    memset(buffer, 0xcc, sizeof(buffer));
    readcount = 0xdeadbeef;
    status = pNtQueryVirtualMemory(NtCurrentProcess(), buffer, MemorySectionName,
                                   name, sizeof(buffer), &readcount);
    ok(status == STATUS_INVALID_ADDRESS, "got %08x\n", status);
    ok(name->SectionFileName.Length == 0xcccc, "Wrong len %u\n", name->SectionFileName.Length);
    ok(readcount == 0xdeadbeef || broken(readcount == 1024 + sizeof(*name)), /* wow64 */
       "Wrong count %lu\n", readcount);

    readcount = 0xdeadbeef;
    status = pNtQueryVirtualMemory(NtCurrentProcess(), (void *)0x1234, MemorySectionName,
                                   name, sizeof(buffer), &readcount);
    ok(status == STATUS_INVALID_ADDRESS, "got %08x\n", status);
    ok(name->SectionFileName.Length == 0xcccc, "Wrong len %u\n", name->SectionFileName.Length);
    ok(readcount == 0xdeadbeef || broken(readcount == 1024 + sizeof(*name)), /* wow64 */
       "Wrong count %lu\n", readcount);

    readcount = 0xdeadbeef;
    status = pNtQueryVirtualMemory(NtCurrentProcess(), (void *)0x1234, MemorySectionName,
                                   name, sizeof(*name) - 1, &readcount);
    ok(status == STATUS_INVALID_ADDRESS, "got %08x\n", status);
    ok(name->SectionFileName.Length == 0xcccc, "Wrong len %u\n", name->SectionFileName.Length);
    ok(readcount == 0xdeadbeef || broken(readcount == 15), /* wow64 */
       "Wrong count %lu\n", readcount);
}

static void test_affinity(void)
{
    NTSTATUS status;
    PROCESS_BASIC_INFORMATION pbi;
    DWORD_PTR proc_affinity, thread_affinity;
    THREAD_BASIC_INFORMATION tbi;
    SYSTEM_INFO si;

    GetSystemInfo(&si);
    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessBasicInformation, &pbi, sizeof(pbi), NULL );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    proc_affinity = pbi.AffinityMask;
    ok( proc_affinity == get_affinity_mask( si.dwNumberOfProcessors ), "Unexpected process affinity\n" );
    proc_affinity = (DWORD_PTR)1 << si.dwNumberOfProcessors;
    status = pNtSetInformationProcess( GetCurrentProcess(), ProcessAffinityMask, &proc_affinity, sizeof(proc_affinity) );
    ok( status == STATUS_INVALID_PARAMETER,
        "Expected STATUS_INVALID_PARAMETER, got %08x\n", status);

    proc_affinity = 0;
    status = pNtSetInformationProcess( GetCurrentProcess(), ProcessAffinityMask, &proc_affinity, sizeof(proc_affinity) );
    ok( status == STATUS_INVALID_PARAMETER,
        "Expected STATUS_INVALID_PARAMETER, got %08x\n", status);

    status = pNtQueryInformationThread( GetCurrentThread(), ThreadBasicInformation, &tbi, sizeof(tbi), NULL );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( tbi.AffinityMask == get_affinity_mask( si.dwNumberOfProcessors ), "Unexpected thread affinity\n" );
    thread_affinity = (DWORD_PTR)1 << si.dwNumberOfProcessors;
    status = pNtSetInformationThread( GetCurrentThread(), ThreadAffinityMask, &thread_affinity, sizeof(thread_affinity) );
    ok( status == STATUS_INVALID_PARAMETER,
        "Expected STATUS_INVALID_PARAMETER, got %08x\n", status);
    thread_affinity = 0;
    status = pNtSetInformationThread( GetCurrentThread(), ThreadAffinityMask, &thread_affinity, sizeof(thread_affinity) );
    ok( status == STATUS_INVALID_PARAMETER,
        "Expected STATUS_INVALID_PARAMETER, got %08x\n", status);

    thread_affinity = 1;
    status = pNtSetInformationThread( GetCurrentThread(), ThreadAffinityMask, &thread_affinity, sizeof(thread_affinity) );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    status = pNtQueryInformationThread( GetCurrentThread(), ThreadBasicInformation, &tbi, sizeof(tbi), NULL );
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( tbi.AffinityMask == 1, "Unexpected thread affinity\n" );

    /* NOTE: Pre-Vista does not allow bits to be set that are higher than the highest set bit in process affinity mask */
    thread_affinity = (pbi.AffinityMask << 1) | pbi.AffinityMask;
    status = pNtSetInformationThread( GetCurrentThread(), ThreadAffinityMask, &thread_affinity, sizeof(thread_affinity) );
    ok( broken(status == STATUS_INVALID_PARAMETER) || (status == STATUS_SUCCESS), "Expected STATUS_SUCCESS, got %08x\n", status );
    if (status == STATUS_SUCCESS)
    {
        status = pNtQueryInformationThread( GetCurrentThread(), ThreadBasicInformation, &tbi, sizeof(tbi), NULL );
        ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status );
        ok( tbi.AffinityMask == pbi.AffinityMask, "Unexpected thread affinity. Expected %lx, got %lx\n", pbi.AffinityMask, tbi.AffinityMask );
    }

    thread_affinity = ~(DWORD_PTR)0 - 1;
    status = pNtSetInformationThread( GetCurrentThread(), ThreadAffinityMask, &thread_affinity, sizeof(thread_affinity) );
    ok( broken(status == STATUS_INVALID_PARAMETER) || (status == STATUS_SUCCESS), "Expected STATUS_SUCCESS, got %08x\n", status );
    if (status == STATUS_SUCCESS)
    {
        status = pNtQueryInformationThread( GetCurrentThread(), ThreadBasicInformation, &tbi, sizeof(tbi), NULL );
        ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status );
        ok( tbi.AffinityMask == (pbi.AffinityMask & (~(DWORD_PTR)0 - 1)), "Unexpected thread affinity. Expected %lx, got %lx\n", pbi.AffinityMask & (~(DWORD_PTR)0 - 1), tbi.AffinityMask );
    }

    /* NOTE: Pre-Vista does not recognize the "all processors" flag (all bits set) */
    thread_affinity = ~(DWORD_PTR)0;
    status = pNtSetInformationThread( GetCurrentThread(), ThreadAffinityMask, &thread_affinity, sizeof(thread_affinity) );
    ok( broken(status == STATUS_INVALID_PARAMETER) || status == STATUS_SUCCESS,
        "Expected STATUS_SUCCESS, got %08x\n", status);

    if (si.dwNumberOfProcessors <= 1)
    {
        skip("only one processor, skipping affinity testing\n");
        return;
    }

    /* Test thread affinity mask resulting from "all processors" flag */
    if (status == STATUS_SUCCESS)
    {
        status = pNtQueryInformationThread( GetCurrentThread(), ThreadBasicInformation, &tbi, sizeof(tbi), NULL );
        ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
        ok( tbi.AffinityMask == get_affinity_mask( si.dwNumberOfProcessors ), "unexpected affinity %#lx\n", tbi.AffinityMask );
    }
    else
        skip("Cannot test thread affinity mask for 'all processors' flag\n");

    proc_affinity = 2;
    status = pNtSetInformationProcess( GetCurrentProcess(), ProcessAffinityMask, &proc_affinity, sizeof(proc_affinity) );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    status = pNtQueryInformationProcess( GetCurrentProcess(), ProcessBasicInformation, &pbi, sizeof(pbi), NULL );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    proc_affinity = pbi.AffinityMask;
    ok( proc_affinity == 2, "Unexpected process affinity\n" );
    /* Setting the process affinity changes the thread affinity to match */
    status = pNtQueryInformationThread( GetCurrentThread(), ThreadBasicInformation, &tbi, sizeof(tbi), NULL );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( tbi.AffinityMask == 2, "Unexpected thread affinity\n" );
    /* The thread affinity is restricted to the process affinity */
    thread_affinity = 1;
    status = pNtSetInformationThread( GetCurrentThread(), ThreadAffinityMask, &thread_affinity, sizeof(thread_affinity) );
    ok( status == STATUS_INVALID_PARAMETER,
        "Expected STATUS_INVALID_PARAMETER, got %08x\n", status);

    proc_affinity = get_affinity_mask( si.dwNumberOfProcessors );
    status = pNtSetInformationProcess( GetCurrentProcess(), ProcessAffinityMask, &proc_affinity, sizeof(proc_affinity) );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    /* Resetting the process affinity also resets the thread affinity */
    status = pNtQueryInformationThread( GetCurrentThread(), ThreadBasicInformation, &tbi, sizeof(tbi), NULL );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok( tbi.AffinityMask == get_affinity_mask( si.dwNumberOfProcessors ),
        "Unexpected thread affinity\n" );
}

static DWORD WINAPI hide_from_debugger_thread(void *arg)
{
    HANDLE stop_event = arg;
    WaitForSingleObject( stop_event, INFINITE );
    return 0;
}

static void test_HideFromDebugger(void)
{
    NTSTATUS status;
    HANDLE thread, stop_event;
    ULONG dummy;

    dummy = 0;
    status = pNtSetInformationThread( GetCurrentThread(), ThreadHideFromDebugger, &dummy, sizeof(ULONG) );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status );
    dummy = 0;
    status = pNtSetInformationThread( GetCurrentThread(), ThreadHideFromDebugger, &dummy, 1 );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status );
    status = pNtSetInformationThread( (HANDLE)0xdeadbeef, ThreadHideFromDebugger, NULL, 0 );
    ok( status == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got %08x\n", status );
    status = pNtSetInformationThread( GetCurrentThread(), ThreadHideFromDebugger, NULL, 0 );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status );
    dummy = 0;
    status = NtQueryInformationThread( GetCurrentThread(), ThreadHideFromDebugger, &dummy, sizeof(ULONG), NULL );
    if (status == STATUS_INVALID_INFO_CLASS)
    {
        win_skip("ThreadHideFromDebugger not available\n");
        return;
    }

    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status );
    dummy = 0;
    status = NtQueryInformationThread( (HANDLE)0xdeadbeef, ThreadHideFromDebugger, &dummy, sizeof(ULONG), NULL );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status );
    dummy = 0;
    status = NtQueryInformationThread( GetCurrentThread(), ThreadHideFromDebugger, &dummy, 1, NULL );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status );
    ok( dummy == 1, "Expected dummy == 1, got %08x\n", dummy );

    stop_event = CreateEventA( NULL, FALSE, FALSE, NULL );
    ok( stop_event != NULL, "CreateEvent failed\n" );
    thread = CreateThread( NULL, 0, hide_from_debugger_thread, stop_event, 0, NULL );
    ok( thread != INVALID_HANDLE_VALUE, "CreateThread failed with %d\n", GetLastError() );

    dummy = 0;
    status = NtQueryInformationThread( thread, ThreadHideFromDebugger, &dummy, 1, NULL );
    ok( status == STATUS_SUCCESS, "got %#x\n", status );
    ok( dummy == 0, "Expected dummy == 0, got %08x\n", dummy );

    status = pNtSetInformationThread( thread, ThreadHideFromDebugger, NULL, 0 );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status );

    dummy = 0;
    status = NtQueryInformationThread( thread, ThreadHideFromDebugger, &dummy, 1, NULL );
    ok( status == STATUS_SUCCESS, "got %#x\n", status );
    ok( dummy == 1, "Expected dummy == 1, got %08x\n", dummy );

    SetEvent( stop_event );
    WaitForSingleObject( thread, INFINITE );
    CloseHandle( thread );
    CloseHandle( stop_event );
}

static void test_NtGetCurrentProcessorNumber(void)
{
    NTSTATUS status;
    SYSTEM_INFO si;
    PROCESS_BASIC_INFORMATION pbi;
    THREAD_BASIC_INFORMATION tbi;
    DWORD_PTR old_process_mask;
    DWORD_PTR old_thread_mask;
    DWORD_PTR new_mask;
    ULONG current_cpu;
    ULONG i;

    if (!pNtGetCurrentProcessorNumber) {
        win_skip("NtGetCurrentProcessorNumber not available\n");
        return;
    }

    GetSystemInfo(&si);
    current_cpu = pNtGetCurrentProcessorNumber();
    if (winetest_debug > 1) trace("dwNumberOfProcessors: %d, current processor: %d\n", si.dwNumberOfProcessors, current_cpu);

    status = pNtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformation, &pbi, sizeof(pbi), NULL);
    old_process_mask = pbi.AffinityMask;
    ok(status == STATUS_SUCCESS, "got 0x%x (expected STATUS_SUCCESS)\n", status);

    status = pNtQueryInformationThread(GetCurrentThread(), ThreadBasicInformation, &tbi, sizeof(tbi), NULL);
    old_thread_mask = tbi.AffinityMask;
    ok(status == STATUS_SUCCESS, "got 0x%x (expected STATUS_SUCCESS)\n", status);

    /* allow the test to run on all processors */
    new_mask = get_affinity_mask( si.dwNumberOfProcessors );
    status = pNtSetInformationProcess(GetCurrentProcess(), ProcessAffinityMask, &new_mask, sizeof(new_mask));
    ok(status == STATUS_SUCCESS, "got 0x%x (expected STATUS_SUCCESS)\n", status);

    for (i = 0; i < si.dwNumberOfProcessors; i++)
    {
        new_mask = (DWORD_PTR)1 << i;
        status = pNtSetInformationThread(GetCurrentThread(), ThreadAffinityMask, &new_mask, sizeof(new_mask));
        ok(status == STATUS_SUCCESS, "%d: got 0x%x (expected STATUS_SUCCESS)\n", i, status);

        status = pNtQueryInformationThread(GetCurrentThread(), ThreadBasicInformation, &tbi, sizeof(tbi), NULL);
        ok(status == STATUS_SUCCESS, "%d: got 0x%x (expected STATUS_SUCCESS)\n", i, status);

        current_cpu = pNtGetCurrentProcessorNumber();
        ok((current_cpu == i), "%d (new_mask 0x%lx): running on processor %d (AffinityMask: 0x%lx)\n",
                                i, new_mask, current_cpu, tbi.AffinityMask);
    }

    /* restore old values */
    status = pNtSetInformationProcess(GetCurrentProcess(), ProcessAffinityMask, &old_process_mask, sizeof(old_process_mask));
    ok(status == STATUS_SUCCESS, "got 0x%x (expected STATUS_SUCCESS)\n", status);

    status = pNtSetInformationThread(GetCurrentThread(), ThreadAffinityMask, &old_thread_mask, sizeof(old_thread_mask));
    ok(status == STATUS_SUCCESS, "got 0x%x (expected STATUS_SUCCESS)\n", status);
}

static void test_ThreadEnableAlignmentFaultFixup(void)
{
    NTSTATUS status;
    ULONG dummy;

    dummy = 0;
    status = NtQueryInformationThread( GetCurrentThread(), ThreadEnableAlignmentFaultFixup, &dummy, sizeof(ULONG), NULL );
    ok( status == STATUS_INVALID_INFO_CLASS || broken(STATUS_NOT_IMPLEMENTED), "Expected STATUS_INVALID_INFO_CLASS, got %08x\n", status );
    status = NtQueryInformationThread( GetCurrentThread(), ThreadEnableAlignmentFaultFixup, &dummy, 1, NULL );
    ok( status == STATUS_INVALID_INFO_CLASS || broken(STATUS_NOT_IMPLEMENTED), "Expected STATUS_INVALID_INFO_CLASS, got %08x\n", status );

    dummy = 1;
    status = pNtSetInformationThread( GetCurrentThread(), ThreadEnableAlignmentFaultFixup, &dummy, sizeof(ULONG) );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status );
    status = pNtSetInformationThread( (HANDLE)0xdeadbeef, ThreadEnableAlignmentFaultFixup, NULL, 0 );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status );
    status = pNtSetInformationThread( (HANDLE)0xdeadbeef, ThreadEnableAlignmentFaultFixup, NULL, 1 );
    ok( status == STATUS_ACCESS_VIOLATION, "Expected STATUS_ACCESS_VIOLATION, got %08x\n", status );
    status = pNtSetInformationThread( (HANDLE)0xdeadbeef, ThreadEnableAlignmentFaultFixup, &dummy, 1 );
    todo_wine ok( status == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got %08x\n", status );
    status = pNtSetInformationThread( GetCurrentProcess(), ThreadEnableAlignmentFaultFixup, &dummy, 1 );
    todo_wine ok( status == STATUS_OBJECT_TYPE_MISMATCH, "Expected STATUS_OBJECT_TYPE_MISMATCH, got %08x\n", status );
    dummy = 1;
    status = pNtSetInformationThread( GetCurrentThread(), ThreadEnableAlignmentFaultFixup, &dummy, 1 );
    ok( status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status );

    dummy = 0;
    status = pNtSetInformationThread( GetCurrentProcess(), ThreadEnableAlignmentFaultFixup, &dummy, 8 );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "Expected STATUS_INFO_LENGTH_MISMATCH, got %08x\n", status );
}

static DWORD WINAPI start_address_thread(void *arg)
{
    PRTL_THREAD_START_ROUTINE entry;
    NTSTATUS status;
    DWORD ret;

    entry = NULL;
    ret = 0xdeadbeef;
    status = pNtQueryInformationThread(GetCurrentThread(), ThreadQuerySetWin32StartAddress,
                                       &entry, sizeof(entry), &ret);
    ok(status == STATUS_SUCCESS, "expected STATUS_SUCCESS, got %08x\n", status);
    ok(ret == sizeof(entry), "NtQueryInformationThread returned %u bytes\n", ret);
    ok(entry == (void *)start_address_thread, "expected %p, got %p\n", start_address_thread, entry);
    return 0;
}

static void test_thread_start_address(void)
{
    PRTL_THREAD_START_ROUTINE entry, expected_entry;
    IMAGE_NT_HEADERS *nt;
    NTSTATUS status;
    HANDLE thread;
    void *module;
    DWORD ret;

    module = GetModuleHandleA(0);
    ok(module != NULL, "expected non-NULL address for module\n");
    nt = RtlImageNtHeader(module);
    ok(nt != NULL, "expected non-NULL address for NT header\n");

    entry = NULL;
    ret = 0xdeadbeef;
    status = pNtQueryInformationThread(GetCurrentThread(), ThreadQuerySetWin32StartAddress,
                                       &entry, sizeof(entry), &ret);
    ok(status == STATUS_SUCCESS, "expected STATUS_SUCCESS, got %08x\n", status);
    ok(ret == sizeof(entry), "NtQueryInformationThread returned %u bytes\n", ret);
    expected_entry = (void *)((char *)module + nt->OptionalHeader.AddressOfEntryPoint);
    ok(entry == expected_entry, "expected %p, got %p\n", expected_entry, entry);

    entry = (void *)0xdeadbeef;
    status = pNtSetInformationThread(GetCurrentThread(), ThreadQuerySetWin32StartAddress,
                                     &entry, sizeof(entry));
    ok(status == STATUS_SUCCESS || status == STATUS_INVALID_PARAMETER, /* >= Vista */
       "expected STATUS_SUCCESS or STATUS_INVALID_PARAMETER, got %08x\n", status);

    if (status == STATUS_SUCCESS)
    {
        entry = NULL;
        ret = 0xdeadbeef;
        status = pNtQueryInformationThread(GetCurrentThread(), ThreadQuerySetWin32StartAddress,
                                           &entry, sizeof(entry), &ret);
        ok(status == STATUS_SUCCESS, "expected STATUS_SUCCESS, got %08x\n", status);
        ok(ret == sizeof(entry), "NtQueryInformationThread returned %u bytes\n", ret);
        ok(entry == (void *)0xdeadbeef, "expected 0xdeadbeef, got %p\n", entry);
    }

    thread = CreateThread(NULL, 0, start_address_thread, NULL, 0, NULL);
    ok(thread != INVALID_HANDLE_VALUE, "CreateThread failed with %d\n", GetLastError());
    ret = WaitForSingleObject(thread, 1000);
    ok(ret == WAIT_OBJECT_0, "expected WAIT_OBJECT_0, got %u\n", ret);
    CloseHandle(thread);
}

static void test_query_data_alignment(void)
{
    ULONG ReturnLength;
    NTSTATUS status;
    DWORD value;

    value = 0xdeadbeef;
    status = pNtQuerySystemInformation(SystemRecommendedSharedDataAlignment, &value, sizeof(value), &ReturnLength);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS, got %08x\n", status);
    ok(sizeof(value) == ReturnLength, "Inconsistent length %u\n", ReturnLength);
#ifdef __arm__
    ok(value == 32, "Expected 32, got %u\n", value);
#else
    ok(value == 64, "Expected 64, got %u\n", value);
#endif
}

static void test_thread_lookup(void)
{
    OBJECT_BASIC_INFORMATION obj_info;
    THREAD_BASIC_INFORMATION info;
    OBJECT_ATTRIBUTES attr;
    CLIENT_ID cid;
    HANDLE handle;
    NTSTATUS status;

    InitializeObjectAttributes( &attr, NULL, 0, NULL, NULL );
    cid.UniqueProcess = ULongToHandle(GetCurrentProcessId());
    cid.UniqueThread = ULongToHandle(GetCurrentThreadId());
    status = pNtOpenThread(&handle, THREAD_QUERY_INFORMATION, &attr, &cid);
    ok(!status, "NtOpenThread returned %#x\n", status);

    status = pNtQueryObject(handle, ObjectBasicInformation, &obj_info, sizeof(obj_info), NULL);
    ok(!status, "NtQueryObject returned: %#x\n", status);
    ok(obj_info.GrantedAccess == (THREAD_QUERY_LIMITED_INFORMATION | THREAD_QUERY_INFORMATION)
       || broken(obj_info.GrantedAccess == THREAD_QUERY_INFORMATION), /* winxp */
       "GrantedAccess = %x\n", obj_info.GrantedAccess);

    status = pNtQueryInformationThread(handle, ThreadBasicInformation, &info, sizeof(info), NULL);
    ok(!status, "NtQueryInformationThread returned %#x\n", status);
    ok(info.ClientId.UniqueProcess == ULongToHandle(GetCurrentProcessId()),
       "UniqueProcess = %p expected %x\n", info.ClientId.UniqueProcess, GetCurrentProcessId());
    ok(info.ClientId.UniqueThread == ULongToHandle(GetCurrentThreadId()),
       "UniqueThread = %p expected %x\n", info.ClientId.UniqueThread, GetCurrentThreadId());
    pNtClose(handle);

    cid.UniqueProcess = 0;
    cid.UniqueThread = ULongToHandle(GetCurrentThreadId());
    status = pNtOpenThread(&handle, THREAD_QUERY_INFORMATION, &attr, &cid);
    ok(!status, "NtOpenThread returned %#x\n", status);
    status = pNtQueryInformationThread(handle, ThreadBasicInformation, &info, sizeof(info), NULL);
    ok(!status, "NtQueryInformationThread returned %#x\n", status);
    ok(info.ClientId.UniqueProcess == ULongToHandle(GetCurrentProcessId()),
       "UniqueProcess = %p expected %x\n", info.ClientId.UniqueProcess, GetCurrentProcessId());
    ok(info.ClientId.UniqueThread == ULongToHandle(GetCurrentThreadId()),
       "UniqueThread = %p expected %x\n", info.ClientId.UniqueThread, GetCurrentThreadId());
    pNtClose(handle);

    cid.UniqueProcess = ULongToHandle(0xdeadbeef);
    cid.UniqueThread = ULongToHandle(GetCurrentThreadId());
    status = pNtOpenThread(&handle, THREAD_QUERY_INFORMATION, &attr, &cid);
    todo_wine
    ok(status == STATUS_INVALID_CID, "NtOpenThread returned %#x\n", status);
    if (!status) pNtClose(handle);

    cid.UniqueProcess = 0;
    cid.UniqueThread = ULongToHandle(0xdeadbeef);
    status = pNtOpenThread(&handle, THREAD_QUERY_INFORMATION, &attr, &cid);
    ok(status == STATUS_INVALID_CID || broken(status == STATUS_INVALID_PARAMETER) /* winxp */,
       "NtOpenThread returned %#x\n", status);
}

static void test_thread_info(void)
{
    NTSTATUS status;
    ULONG len, data;

    len = 0xdeadbeef;
    data = 0xcccccccc;
    status = pNtQueryInformationThread( GetCurrentThread(), ThreadAmILastThread,
                                        &data, sizeof(data), &len );
    ok( !status, "failed %x\n", status );
    ok( data == 0 || data == 1, "wrong data %x\n", data );
    ok( len == sizeof(data), "wrong len %u\n", len );

    len = 0xdeadbeef;
    data = 0xcccccccc;
    status = pNtQueryInformationThread( GetCurrentThread(), ThreadAmILastThread,
                                        &data, sizeof(data) - 1, &len );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "failed %x\n", status );
    ok( data == 0xcccccccc, "wrong data %x\n", data );
    ok( len == 0xdeadbeef, "wrong len %u\n", len );

    len = 0xdeadbeef;
    data = 0xcccccccc;
    status = pNtQueryInformationThread( GetCurrentThread(), ThreadAmILastThread,
                                        &data, sizeof(data) + 1, &len );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "failed %x\n", status );
    ok( data == 0xcccccccc, "wrong data %x\n", data );
    ok( len == 0xdeadbeef, "wrong len %u\n", len );
}

static void test_wow64(void)
{
#ifndef _WIN64
    if (is_wow64)
    {
        PEB64 *peb64;
        TEB64 *teb64 = (TEB64 *)NtCurrentTeb()->GdiBatchCount;

        ok( !!teb64, "GdiBatchCount not set\n" );
        ok( (char *)NtCurrentTeb() + NtCurrentTeb()->WowTebOffset == (char *)teb64 ||
            broken(!NtCurrentTeb()->WowTebOffset),  /* pre-win10 */
            "wrong WowTebOffset %x (%p/%p)\n", NtCurrentTeb()->WowTebOffset, teb64, NtCurrentTeb() );
        ok( (char *)teb64 + 0x2000 == (char *)NtCurrentTeb(), "unexpected diff %p / %p\n",
            teb64, NtCurrentTeb() );
        ok( teb64->Tib.ExceptionList == PtrToUlong( NtCurrentTeb() ), "wrong Tib.ExceptionList %s / %p\n",
            wine_dbgstr_longlong(teb64->Tib.ExceptionList), NtCurrentTeb() );
        ok( teb64->Tib.Self == PtrToUlong( teb64 ), "wrong Tib.Self %s / %p\n",
            wine_dbgstr_longlong(teb64->Tib.Self), teb64 );
        ok( teb64->StaticUnicodeString.Buffer == PtrToUlong( teb64->StaticUnicodeBuffer ),
            "wrong StaticUnicodeString %s / %p\n",
            wine_dbgstr_longlong(teb64->StaticUnicodeString.Buffer), teb64->StaticUnicodeBuffer );
        ok( teb64->ClientId.UniqueProcess == GetCurrentProcessId(), "wrong pid %s / %x\n",
            wine_dbgstr_longlong(teb64->ClientId.UniqueProcess), GetCurrentProcessId() );
        ok( teb64->ClientId.UniqueThread == GetCurrentThreadId(), "wrong tid %s / %x\n",
            wine_dbgstr_longlong(teb64->ClientId.UniqueThread), GetCurrentThreadId() );
        peb64 = ULongToPtr( teb64->Peb );
        ok( peb64->ImageBaseAddress == PtrToUlong( NtCurrentTeb()->Peb->ImageBaseAddress ),
            "wrong ImageBaseAddress %s / %p\n",
            wine_dbgstr_longlong(peb64->ImageBaseAddress), NtCurrentTeb()->Peb->ImageBaseAddress);
        ok( peb64->OSBuildNumber == NtCurrentTeb()->Peb->OSBuildNumber, "wrong OSBuildNumber %x / %x\n",
            peb64->OSBuildNumber, NtCurrentTeb()->Peb->OSBuildNumber );
        ok( peb64->OSPlatformId == NtCurrentTeb()->Peb->OSPlatformId, "wrong OSPlatformId %x / %x\n",
            peb64->OSPlatformId, NtCurrentTeb()->Peb->OSPlatformId );
        return;
    }
#endif
    ok( !NtCurrentTeb()->GdiBatchCount, "GdiBatchCount set to %x\n", NtCurrentTeb()->GdiBatchCount );
    ok( !NtCurrentTeb()->WowTebOffset || broken( NtCurrentTeb()->WowTebOffset == 1 ), /* vista */
        "WowTebOffset set to %x\n", NtCurrentTeb()->WowTebOffset );
}

static void test_debug_object(void)
{
    NTSTATUS status;
    HANDLE handle;
    OBJECT_ATTRIBUTES attr = { sizeof(attr) };
    ULONG len, flag = 0;
    DBGUI_WAIT_STATE_CHANGE state;
    DEBUG_EVENT event;

    status = pNtCreateDebugObject( &handle, DEBUG_ALL_ACCESS, &attr, 0 );
    ok( !status, "NtCreateDebugObject failed %x\n", status );
    status = pNtSetInformationDebugObject( handle, 0, &flag, sizeof(ULONG), &len );
    ok( status == STATUS_INVALID_PARAMETER, "NtSetInformationDebugObject failed %x\n", status );
    status = pNtSetInformationDebugObject( handle, 2, &flag, sizeof(ULONG), &len );
    ok( status == STATUS_INVALID_PARAMETER, "NtSetInformationDebugObject failed %x\n", status );
    status = pNtSetInformationDebugObject( (HANDLE)0xdead, DebugObjectKillProcessOnExitInformation,
                                           &flag, sizeof(ULONG), &len );
    ok( status == STATUS_INVALID_HANDLE, "NtSetInformationDebugObject failed %x\n", status );

    len = 0xdead;
    status = pNtSetInformationDebugObject( handle, DebugObjectKillProcessOnExitInformation,
                                           &flag, sizeof(ULONG) + 1, &len );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "NtSetInformationDebugObject failed %x\n", status );
    ok( len == sizeof(ULONG), "wrong len %u\n", len );

    len = 0xdead;
    status = pNtSetInformationDebugObject( handle, DebugObjectKillProcessOnExitInformation,
                                           &flag, sizeof(ULONG) - 1, &len );
    ok( status == STATUS_INFO_LENGTH_MISMATCH, "NtSetInformationDebugObject failed %x\n", status );
    ok( len == sizeof(ULONG), "wrong len %u\n", len );

    len = 0xdead;
    status = pNtSetInformationDebugObject( handle, DebugObjectKillProcessOnExitInformation,
                                           &flag, sizeof(ULONG), &len );
    ok( !status, "NtSetInformationDebugObject failed %x\n", status );
    ok( !len, "wrong len %u\n", len );

    flag = DEBUG_KILL_ON_CLOSE;
    status = pNtSetInformationDebugObject( handle, DebugObjectKillProcessOnExitInformation,
                                           &flag, sizeof(ULONG), &len );
    ok( !status, "NtSetInformationDebugObject failed %x\n", status );
    ok( !len, "wrong len %u\n", len );

    for (flag = 2; flag; flag <<= 1)
    {
        status = pNtSetInformationDebugObject( handle, DebugObjectKillProcessOnExitInformation,
                                               &flag, sizeof(ULONG), &len );
        ok( status == STATUS_INVALID_PARAMETER, "NtSetInformationDebugObject failed %x\n", status );
    }

    pNtClose( handle );

    memset( &state, 0xdd, sizeof(state) );
    state.NewState = DbgIdle;
    memset( &event, 0xcc, sizeof(event) );
    status = pDbgUiConvertStateChangeStructure( &state, &event );
    ok( status == STATUS_UNSUCCESSFUL, "DbgUiConvertStateChangeStructure failed %x\n", status );
    ok( event.dwProcessId == 0xdddddddd, "event not updated %x\n", event.dwProcessId );
    ok( event.dwThreadId == 0xdddddddd, "event not updated %x\n", event.dwThreadId );

    state.NewState = DbgReplyPending;
    memset( &event, 0xcc, sizeof(event) );
    status = pDbgUiConvertStateChangeStructure( &state, &event );
    ok( status == STATUS_UNSUCCESSFUL, "DbgUiConvertStateChangeStructure failed %x\n", status );
    ok( event.dwProcessId == 0xdddddddd, "event not updated %x\n", event.dwProcessId );
    ok( event.dwThreadId == 0xdddddddd, "event not updated %x\n", event.dwThreadId );

    state.NewState = 11;
    memset( &event, 0xcc, sizeof(event) );
    status = pDbgUiConvertStateChangeStructure( &state, &event );
    ok( status == STATUS_UNSUCCESSFUL, "DbgUiConvertStateChangeStructure failed %x\n", status );
    ok( event.dwProcessId == 0xdddddddd, "event not updated %x\n", event.dwProcessId );
    ok( event.dwThreadId == 0xdddddddd, "event not updated %x\n", event.dwThreadId );

    state.NewState = DbgExitProcessStateChange;
    state.StateInfo.ExitProcess.ExitStatus = 0x123456;
    status = pDbgUiConvertStateChangeStructure( &state, &event );
    ok( !status, "DbgUiConvertStateChangeStructure failed %x\n", status );
    ok( event.dwProcessId == 0xdddddddd, "event not updated %x\n", event.dwProcessId );
    ok( event.dwThreadId == 0xdddddddd, "event not updated %x\n", event.dwThreadId );
    ok( event.u.ExitProcess.dwExitCode == 0x123456, "event not updated %x\n", event.u.ExitProcess.dwExitCode );

    memset( &state, 0xdd, sizeof(state) );
    state.NewState = DbgCreateProcessStateChange;
    status = pDbgUiConvertStateChangeStructure( &state, &event );
    ok( !status, "DbgUiConvertStateChangeStructure failed %x\n", status );
    ok( event.dwProcessId == 0xdddddddd, "event not updated %x\n", event.dwProcessId );
    ok( event.dwThreadId == 0xdddddddd, "event not updated %x\n", event.dwThreadId );
    ok( event.u.CreateProcessInfo.nDebugInfoSize == 0xdddddddd, "event not updated %x\n", event.u.CreateProcessInfo.nDebugInfoSize );
    ok( event.u.CreateProcessInfo.lpThreadLocalBase == NULL, "event not updated %p\n", event.u.CreateProcessInfo.lpThreadLocalBase );
    ok( event.u.CreateProcessInfo.lpImageName == NULL, "event not updated %p\n", event.u.CreateProcessInfo.lpImageName );
    ok( event.u.CreateProcessInfo.fUnicode == TRUE, "event not updated %x\n", event.u.CreateProcessInfo.fUnicode );

    memset( &state, 0xdd, sizeof(state) );
    state.NewState = DbgLoadDllStateChange;
    status = pDbgUiConvertStateChangeStructure( &state, &event );
    ok( !status, "DbgUiConvertStateChangeStructure failed %x\n", status );
    ok( event.dwProcessId == 0xdddddddd, "event not updated %x\n", event.dwProcessId );
    ok( event.dwThreadId == 0xdddddddd, "event not updated %x\n", event.dwThreadId );
    ok( event.u.LoadDll.nDebugInfoSize == 0xdddddddd, "event not updated %x\n", event.u.LoadDll.nDebugInfoSize );
    ok( PtrToUlong(event.u.LoadDll.lpImageName) == 0xdddddddd, "event not updated %p\n", event.u.LoadDll.lpImageName );
    ok( event.u.LoadDll.fUnicode == TRUE, "event not updated %x\n", event.u.LoadDll.fUnicode );
}

START_TEST(info)
{
    char **argv;
    int argc;

    if(!InitFunctionPtrs())
        return;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3) return; /* Child */

    /* NtQuerySystemInformation */
    test_query_basic();
    test_query_cpu();
    test_query_performance();
    test_query_timeofday();
    test_query_process();
    test_query_procperf();
    test_query_module();
    test_query_handle();
    test_query_cache();
    test_query_interrupt();
    test_time_adjustment();
    test_query_kerndebug();
    test_query_regquota();
    test_query_logicalproc();
    test_query_logicalprocex();
    test_query_firmware();
    test_query_data_alignment();

    /* NtPowerInformation */
    test_query_battery();
    test_query_processor_power_info();

    /* NtQueryInformationProcess */
    test_query_process_basic();
    test_query_process_io();
    test_query_process_vm();
    test_query_process_times();
    test_query_process_debug_port(argc, argv);
    test_query_process_priority();
    test_query_process_handlecount();
    test_query_process_wow64();
    test_query_process_image_file_name();
    test_query_process_debug_object_handle(argc, argv);
    test_query_process_debug_flags(argc, argv);
    test_query_process_image_info();
    test_mapprotection();
    test_threadstack();

    /* NtQueryInformationThread */
    test_thread_info();
    test_HideFromDebugger();
    test_thread_start_address();
    test_thread_lookup();

    test_affinity();
    test_wow64();
    test_debug_object();

    /* belongs to its own file */
    test_readvirtualmemory();
    test_queryvirtualmemory();
    test_NtGetCurrentProcessorNumber();

    test_ThreadEnableAlignmentFaultFixup();
}
