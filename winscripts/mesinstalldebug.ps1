 #!/usr/bin/env powershell

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Execute this script from the parent directory by invoking:
#   winscripts/mesinstalldebug.ps1

# Use "dot source" to include another file
. $PSScriptRoot/helpers/mesdebugsetdirvars.ps1
. $PSScriptRoot/helpers/adjbuilddirformesbuild.ps1

Write-Host "Using debug build dir $MESON_BUILD_DIR"

# Installs to [C]:\Program Files\pistache_distribution

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
