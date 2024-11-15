#!/usr/bin/env powershell

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# installman.ps1 is used by the build process under Windows to install
# pistachelog.dll and the Pistache logging manifest

# Usage:
# installman.ps1 -dirinpstlogdll <dll path> -inpstlogdll <dll name> `
#                -inpstman <manifest path> `
#                -outpstmaninst <file into which date+time written>

# This script is expected to be run at build time ("meson
# compile..."), not at install time ("meson install..."). By
# installing these logging components at build time, we can use
# logging while debugging Pistache code, prior to installing the whole
# Pistache package to its place in the OS.

# Note: the outpstmaninst file is created purely so that the file's
# date+time stamp can record when this script copied pistachelog.dll
# to its correct location and installed the corresponding logging
# manifest. So then the build system can make the outpstmaninst file
# dependent on the manifest definition file, such that, if the
# manifest definition file is changed, this script will be rerun and
# pistachelog.dll and the installed manifest will be updated.

param (
    [Parameter(Mandatory=$true)][string]$dirinpstlogdll,
    [Parameter(Mandatory=$true)][string]$inpstlogdll,
    [Parameter(Mandatory=$true)][string]$inpstman,
    [Parameter(Mandatory=$true)][string]$outpstmaninst
)

if (-Not (Test-Path -Path "$dirinpstlogdll"))
{
    throw "Directory $dirinpstlogdll not found"
}

$fpinpstlogdll = Join-Path -Path "$dirinpstlogdll" -ChildPath "$inpstlogdll"

Write-Host "fpinpstlogdll is $fpinpstlogdll"
if (-Not (Test-Path -Path "$fpinpstlogdll"))
{
    throw "DLL not found at $fpinpstlogdll"
}

$logdll_name = Split-Path -Path "$fpinpstlogdll" -Leaf
if ("$logdll_name" -ne "pistachelog.dll")
{
    $alt_logdll_name = "pistachelog.dll"
}

if (-Not (Test-Path "$inpstman"))
{
    throw "Pistache manifest file not found at $inpstman"
}

if (-Not (Test-Path "$env:ProgramFiles\pistache_distribution"))
{
    mkdir "$env:ProgramFiles\pistache_distribution"
}
if (-Not (Test-Path "$env:ProgramFiles\pistache_distribution\bin"))
{
    mkdir "$env:ProgramFiles\pistache_distribution\bin"
}

# Check we'll be able to write $outpstmaninst
# And remove the old one, if any
if (-Not (Test-Path "$outpstmaninst"))
{
    "Dummy" | out-file -Encoding ascii -filepath "$outpstmaninst"
}
rm "$outpstmaninst"

cp "$fpinpstlogdll" "$env:ProgramFiles\pistache_distribution\bin\."
if ($alt_logdll_name)
{
    cp "$fpinpstlogdll" `
      "$env:ProgramFiles\pistache_distribution\bin\$alt_logdll_name"
}

wevtutil um "$inpstman" # Uninstall - does nothing if not installed
wevtutil im "$inpstman" # Install

# Next we create the Windows Registry log-to-stdout-as-well value for
# Pistache, if it doesn't exist already. If that registry property
# already exists, we don't change it.
if (-Not (Test-Path HKCU:\Software\pistacheio))
{
    $key1 = New-Item -Path HKCU:\Software -Name pistacheio
}
if (Test-Path HKCU:\Software\pistacheio\pistache)
{
    $key2 = Get-Item -Path HKCU:\Software\pistacheio\pistache
}
else
{
    $key2 = New-Item -Path HKCU:\Software\pistacheio -Name pistache
}
if (-Not ($key2.Property -contains "psLogToStdoutAsWell"))
{
    $newItemPropertySplat = @{
        Path = $key2.PSPath
        Name = 'psLogToStdoutAsWell'
        PropertyType = 'DWord'
        Value = 0
    }
    New-ItemProperty @newItemPropertySplat
}

# Finally we create the 'out' marker file
"At $(Get-Date):`r`n  $inpstlogdll copied to $env:ProgramFiles\pistache_distribution\bin\.`r`n  $inpstman logging manifest installed" `
| out-file -Encoding utf8 -width 2560 -filepath "$outpstmaninst"
