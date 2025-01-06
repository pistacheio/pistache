#!/usr/bin/env powershell

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# This script is expected to be run at install time ("meson
# install..."), in contrast to installman.ps1. It doesn't take any
# parameters but uses the environment variables DESTDIR and
# MESON_INSTALL_PREFIX, which are set by "meson install...".
#
# This script doesn't copy pistachelog.dll (which will be put in place
# by the normal "meson install..." mechanisms - except with gcc, where
# the file needs to be renamed due to gcc naming convention); this
# script exists to install the logging manifest file pist_winlog.man
# and set the Windows Registry key property value
# HKCU:\Software\pistacheio\pistache\psLogToStdoutAsWell.

$have_admin_rights = ([Security.Principal.WindowsPrincipal] `
  [Security.Principal.WindowsIdentity]::GetCurrent() `
  ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

$pistinstbase="$env:DESTDIR\$env:MESON_INSTALL_PREFIX"
if (($env:MESON_INSTALL_PREFIX) -and`
    [System.IO.Path]::IsPathRooted($env:MESON_INSTALL_PREFIX))
{ # Ignore DESTDIR if MESON_INSTALL_PREFIX is an absolute path
    $pistinstbase="$env:MESON_INSTALL_PREFIX"
}
Write-Host "pistinstbase is $pistinstbase"

if (-Not (Test-Path -Path "$pistinstbase"))
{
    throw "Pistache install directory not found at $pistinstbase"
}

$pistwinlogman="$pistinstbase\src\winlog\pist_winlog.man"
if (-Not (Test-Path -Path "$pistwinlogman"))
{
    throw "pist_winlog.man not found at $pistwinlogman"
}

$pistachelogdll="$pistinstbase\bin\pistachelog.dll"
$pistacheloggccdll="$pistinstbase\bin\libpistachelog.dll"
if (Test-Path -Path "$pistacheloggccdll")
{
    if (Test-Path -Path "$pistachelogdll")
    {
        $pistachelogdlldate = `
          Get-Item "$pistachelogdll" | Foreach {$_.LastWriteTime}
    }
    if ((! ($pistachelogdlldate)) -or `
      (Test-Path -Path "$pistacheloggccdll" -NewerThan "$pistachelogdlldate"))
    {
        cp "$pistacheloggccdll" "$pistachelogdll"
        Write-Host "Copied $pistacheloggccdll to $pistachelogdll"
    }
}

if (-Not (Test-Path "$pistachelogdll"))
{
    throw "pistachelog.dll not found at $pistachelogdll"
}

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

Write-Host 'Installing Pistache logging manifest into Windows'
if ($have_admin_rights) {
    wevtutil um "$pistwinlogman" # Uninstall - does nothing if not installed
    wevtutil im "$pistwinlogman" # Install
}
else {
    $pst_command = `
      'Write-Host ''In admin shell, installing logging manifest''; ' + `
      'wevtutil um ''' + $pistwinlogman + '''; wevtutil im ''' + `
      $pistwinlogman + '''; if($?) { ' + `
      'Write-Host ''Success; exiting from shell that has Admin rights''; ' + `
      '} else { ' + `
      'Write-Host "Press any key to continue" -ForegroundColor Yellow; ' + `
      '$x = $host.ui.RawUI.ReadKey(''NoEcho,IncludeKeyDown''); ' + `
      'Write-Host ''Error; exiting from shell that has Admin rights'' }; ' + `
      '[Environment]::Exit(0)'

    Write-Host "To install manifest, launching cmd with admin rights"

    # We use "Start-Process" so we can do "-verb RunAs", which provides
    # admin-level privileges, like doing "sudo" on Linux
    # ("wevtutil im ..." requires admin rights)

    $modeproc = Start-Process -FilePath powershell.exe `
      -ArgumentList "-NoProfile", "-noexit", `
      "-WindowStyle", "Normal", `
      "-Command", "$pst_command" `
      -PassThru -verb RunAs

    Write-Host "Wait for cmd to complete, no timeout"
    $modeproc | Wait-Process -ErrorAction SilentlyContinue
    Write-Host "cmd completed"
}
