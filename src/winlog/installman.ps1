#!/usr/bin/env powershell

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Usage:
# installman.ps1 -inpstlogdll <pistachelog.dll path> -inpstman <manifest path>
#                            -outpstmaninst <file into which date+time written>

param (
    [Parameter(Mandatory=$true)][string]$dirinpstlogdll,
    [Parameter(Mandatory=$true)][string]$inpstlogdll,
    [Parameter(Mandatory=$true)][string]$inpstman,
    [Parameter(Mandatory=$true)][string]$outpstmaninst
)

if (-Not (Test-Path -Path "$dirinpstlogdll"))
{
    throw "pistachelog.dll directory not found at $dirinpstlogdll"
}

$fpinpstlogdll = Join-Path -Path "$dirinpstlogdll" -ChildPath "$inpstlogdll"

Write-Host "fpinpstlogdll is $fpinpstlogdll"
if (-Not (Test-Path -Path "$fpinpstlogdll"))
{
    throw "pistachelog.dll not found at $fpinpstlogdll"
}

if (-Not (Test-Path "$fpinpstlogdll"))
{
    throw "Pistache manifest file not found at $inpstman"
}

if (-Not (Test-Path "$env:ProgramFiles\pistache_distribution"))
{
    mkdir "$env:ProgramFiles\pistache_distribution"
}

# Check we'll be able to write $outpstmaninst
# And remove the old one, if any
if (-Not (Test-Path "$outpstmaninst"))
{
    "Dummy" | out-file -Encoding ascii -filepath "$outpstmaninst"
}
rm "$outpstmaninst"

cp "$fpinpstlogdll" "$env:ProgramFiles\pistache_distribution\."

wevtutil um "$inpstman" # Uninstall - does nothing if not installed

wevtutil im "$inpstman" # Install

"At $(Get-Date):`r`n  $inpstlogdll copied to $env:ProgramFiles\pistache_distribution\.`r`n  $inpstman logging manifest installed" `
| out-file -Encoding utf8 -width 2560 -filepath "$outpstmaninst"





