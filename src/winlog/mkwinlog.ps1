#!/usr/bin/env powershell

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# https://kallanreed.com/2016/05/28/creating-an-etw-provider-step-by-step/

Write-Host "Not needed any more - src/meson.build does it for us instead"
return 0

mc.exe -um -h "..\..\include\pistache\" -r ".\" pist_winlog.man
rc.exe 'pist_winlog.rc'
link.exe  /dll /noentry /machine:x64 pist_winlog.res /OUT:'C:\Program Files\pistache_distribution\bin\pistachelog.dll'
