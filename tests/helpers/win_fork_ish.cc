/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "win_fork_ish.h"

#ifdef _WIN32 // defined for 32 or 64 bit Windows

/* ------------------------------------------------------------------------- */

// For NtCreateUserProcess
#include <phnt_windows.h>
#include <phnt.h>

#include <stdio.h> // wprintf_s

/* ------------------------------------------------------------------------- */

// Returns 0 if parent process, 1 if child process, and -1 on error in which
// case errno is set
int pist_simple_create_user_process(HANDLE * processHandle,
                                    HANDLE * threadHandle,
                                    bool inheritHandles)
{
    // Process handling in Windows is pretty different. The closest
    // equivalent to "fork" is using NtCreateUserProcess as below.
    // For further discussion:
    //   https://github.com/huntandhackett/process-cloning
    // For further info (not yet used here):
    // https://captmeelo.com/redteam/maldev/2022/05/10/ntcreateuserprocess.html
    PS_CREATE_INFO createInfo = { 0 };
    createInfo.Size = sizeof(createInfo);

    wprintf_s(L"From the parent: My PID is %zd, TID is %zd\r\n",
              (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess,
              (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread);

    SSIZE_T parent_pid = (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess;
    // SSIZE_T parent_tid = (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread;

    NTSTATUS status = NtCreateUserProcess(
        processHandle,
        threadHandle,
        PROCESS_ALL_ACCESS,
        THREAD_ALL_ACCESS,
        NULL,                                 // ProcessObjectAttributes
        NULL,                                 // ThreadObjectAttributes
        inheritHandles ?
            PROCESS_CREATE_FLAGS_INHERIT_HANDLES : 0, // ProcessFlags
        0,                                    // ThreadFlags
        NULL,                                 // ProcessParameters
        &createInfo,                          
        NULL                                  // AttributeList
        );

    wprintf_s(L"After NtCreateUserProcess. PID is %zd, TID is %zd\r\n",
              (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess,
              (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread);


    SSIZE_T after_pid = (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess;

    if ((NT_SUCCESS(status)) && (after_pid) && (parent_pid != after_pid))
        status = STATUS_PROCESS_CLONED;

    if (status == STATUS_PROCESS_CLONED)
    {
        // Executing inside the clone
        wprintf_s(L"Executing inside the clone");

        // Re-attach to the parent's console to be able to write to it
        FreeConsole();
        AttachConsole(ATTACH_PARENT_PROCESS);

        return(1);

        // Note: To terminate (without cleanup), use:
        //   NtTerminateProcess(NtCurrentProcess(),
        //                      STATUS_PROCESS_CLONED);
    }

    // Executing inside the original/parent process
    wprintf_s(L"Executing inside the clone");

    if (!NT_SUCCESS(status))
    {
        errno = ENOSYS;
        return(-1);
    }

    return(0);
}

/* ------------------------------------------------------------------------- */

#endif // of ifdef _WIN32

