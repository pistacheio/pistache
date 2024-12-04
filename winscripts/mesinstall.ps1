 #!/usr/bin/env powershell

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Execute this script from the parent directory by invoking:
#   winscripts/mesinstall.ps1

# Use "dot source" to include another file
. $PSScriptRoot/helpers/messetdirvars.ps1
. $PSScriptRoot/helpers/adjbuilddirformesbuild.ps1

# Installs to [C]:\Program Files\pistache_distribution

try { meson install -C ${MESON_BUILD_DIR} }
catch {
    Write-Host "Plain meson install failed, trying again with Admin rights"

    # We use "Start-Process" so we can do "-verb RunAs", which provides
    # admin-level privileges, like doing "sudo" on Linux
    $insproc = Start-Process -FilePath powershell.exe -ArgumentList "-Command","meson install -C ${MESON_BUILD_DIR}" -PassThru -verb RunAs

    # keep track of timeout event
    $instimeouted = $null

    # wait up to 60 seconds for normal termination
    $insproc | Wait-Process -Timeout 60 -ErrorAction SilentlyContinue -ErrorVariable instimeouted

    if ($instimeouted)
    {
        $insproc | kill
        Write-Error "Error: meson install timed out"
    }
    elseif ($insproc.ExitCode -ne 0)
    {
        Write-Error "Error: meson install returned error"
    }
}
