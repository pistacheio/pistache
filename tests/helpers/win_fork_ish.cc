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

// Returns 0 if parent process, 1 if child process, and -1 on error in which
// case errno is set
int pist_simple_create_user_process(HANDLE * processHandle,
                                    HANDLE * threadHandle)
{
    // Process handling in Windows is pretty different. The closest
    // equivalent to "fork" is using NtCreateUserProcess as below.
    // For further discussion:
    //   https://github.com/huntandhackett/process-cloning
    PS_CREATE_INFO createInfo = { sizeof(createInfo) };

    NTSTATUS status = NtCreateUserProcess(
        processHandle,
        threadHandle,
        PROCESS_ALL_ACCESS,
        THREAD_ALL_ACCESS,
        NULL,                                 // ProcessObjectAttributes
        NULL,                                 // ThreadObjectAttributes
        PROCESS_CREATE_FLAGS_INHERIT_HANDLES, // ProcessFlags
        0,                                    // ThreadFlags
        NULL,                                 // ProcessParameters
        &createInfo,                          
        NULL                                  // AttributeList
        );

    if (status == STATUS_PROCESS_CLONED)
    {
        // Executing inside the clone

        // Re-attach to the parent's console to be able to write to it
        FreeConsole();
        AttachConsole(ATTACH_PARENT_PROCESS);

        return(1);

        // Note: To terminate (without cleanup), use:
        //   NtTerminateProcess(NtCurrentProcess(),
        //                      STATUS_PROCESS_CLONED);
    }

    // Executing inside the original/parent process

    if (!NT_SUCCESS(status))
    {
        errno = ENOSYS;
        return(-1);
    }

    return(0);
}

/* ------------------------------------------------------------------------- */

#endif // of ifdef _WIN32

