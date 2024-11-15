#!/usr/bin/env powershell

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Setup dev environment for mingw64's gcc

$savedpwd=$pwd

. $PSScriptRoot/helpers/commonsetup.ps1

# Set $env:force_msys_gcc if you want to force the use of msys64's gcc
# even if a different gcc is already installed. (Note:
# $env:force_msys_gcc is used by windows.yaml)
if (($env:force_msys_gcc) -or `
  (! (Get-Command gcc.exe -errorAction SilentlyContinue))) {
      if (Test-Path -Path "$env:SYSTEMDRIVE\msys64") {
          $msys64_dir = "$env:SYSTEMDRIVE\msys64"
      }
      elseif (Test-Path -Path "$env:HOMEDRIVE\msys64") {
          $msys64_dir = "$env:HOMEDRIVE\msys64"
      }
      else {
          cd ~
          Write-Host "Downloading msys2-x86_64-latest.sfx.exe"
          Invoke-WebRequest -Uri `
            "https://repo.msys2.org/distrib/msys2-x86_64-latest.sfx.exe" `
            -Outfile "msys2-x86_64-latest.sfx.exe"
          Write-Host "Self-extracting msys2-x86_64-latest.sfx.exe"
          .\msys2-x86_64-latest.sfx.exe -y -o$env:SYSTEMDRIVE\
          if (Test-Path -Path "$env:SYSTEMDRIVE\msys64") {
              $msys64_dir = "$env:SYSTEMDRIVE\msys64"
          }
          else {
              throw "msys64 didn't install as expected?"
          }
          Write-Host "Checking msys2 shell"
          & $msys64_dir\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c “echo msysFirstTerminal”
      }

      if (! (Test-Path -Path "$msys64_dir\ucrt64\bin\gcc.exe")) {
          Write-Host "Installing gcc"
          & $msys64_dir\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c "pacman --noconfirm -S mingw-w64-ucrt-x86_64-gcc"
          if (! (Test-Path -Path "$msys64_dir\ucrt64\bin\gcc.exe")) {
              throw "gcc didn't install in msys as expected?"
          }
      }

      if (! (Test-Path -Path "$msys64_dir\ucrt64\bin\gdb.exe")) {
          Write-Host "Installing gdb"
          & $msys64_dir\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c "pacman --noconfirm -S mingw-w64-ucrt-x86_64-gdb"
      }

      $env:PATH="$msys64_dir\ucrt64\bin;$env:PATH"
  }

$env:CXX="g++"
$env:CC="gcc"

$savedpwd = $pwd

if (! (Get-Command mc.exe -errorAction SilentlyContinue)) {
    if (Test-Path -Path "$env:ProgramFiles\Windows Kits") {
        $win_sdk_found=1
        cd "$env:ProgramFiles\Windows Kits"
        $mc_exes_found=Get-ChildItem -Path "mc.exe" -Recurse |`
          Sort-Object -Descending -Property LastWriteTime
        foreach ($mc_exe_found in $mc_exes_found) {
            if ($mc_exe_found -like "*\x64\mc*") {
                $mc_exe = $mc_exe_found
                break
            }
            if ((! ($mc_exe)) -and ($mc_exe_found -like "*\x86\mc*")) {
	        $mc_exe = $mc_exe_found
	    }
        }
    }

    if (! ($mc_exe)) {
        if (Test-Path -Path "${env:ProgramFiles(x86)}\Windows Kits") {
            $win_sdk_found=1
            cd "${env:ProgramFiles(x86)}\Windows Kits"
            $mc_exes_found=Get-ChildItem -Path "mc.exe" -Recurse |`
              Sort-Object -Descending -Property LastWriteTime
            foreach ($mc_exe_found in $mc_exes_found) {
                if ($mc_exe_found -like "*\x64\mc*") {
                    $mc_exe = $mc_exe_found
                    break
                }
                if ((! ($mc_exe)) -and ($mc_exe_found -like "*\x86\mc*")) {
	            $mc_exe = $mc_exe_found
	        }
            }
        }
    }
}

cd "$savedpwd"

if (! ($win_sdk_found)) {
    throw "Unable to find Windows Kits (SDKs) folder"
}
if (! ($mc_exe)) {
    throw "Unable to find mc.exe in Windows Kits (SDKs)"
}

$mc_exe_dir = Split-Path -Path $mc_exe
$env:PATH="$env:PATH;$mc_exe_dir"

if (! (Get-Command ninja -errorAction SilentlyContinue)) {
    if (($env:VCPKG_DIR) -And (Test-Path -Path "$env:VCPKG_DIR\installed")) {
        cd "$env:VCPKG_DIR\installed"

        $ninja_dir=Get-ChildItem -Path "ninja.exe" -Recurse | `
          Select -ExpandProperty "FullName" | Sort-Object { $_.Length } | `
          Select -Index 0 | Split-Path
    }

    if ((! ($ninja_dir)) -or (! (Test-Path -Path $ninja_dir))) {
        # Can't find ninja in "$env:VCPKG_DIR\installed"; install it now

        if (Get-Command winget -errorAction SilentlyContinue) {
            winget install "Ninja-build.Ninja";
            # Don't set ninja_dir - leaving it empty will mean it
            # doesn't get added to $env:path below, which is good
            # since winget takes care of the path for us
        }
        else {
            if (! (vcpkg list "vcpkg-tool-ninja")) {
                vcpkg install vcpkg-tool-ninja

                if (($env:VCPKG_DIR) -And `
                  (Test-Path -Path "$env:VCPKG_DIR\installed")) {
                      cd "$env:VCPKG_DIR\installed"

                      $ninja_dir = Get-ChildItem -Path "ninja.exe" -Recurse | `
                        Select -ExpandProperty "FullName" | `
                        Sort-Object { $_.Length } | Select -Index 0 `
                        | Split-Path
                  }
            }
            else {
                Write-Host `
                  "WARNING: ninja already installed by vcpkg, but not found?"
            }
        }
    }

    if (($ninja_dir) -And (Test-Path -Path $ninja_dir)) {
        $env:Path="$ninja_dir;$env:Path" # Add ninja.exe to Path
    }

    cd "$savedpwd"
}

if ((! ($env:plain_prompt)) -or ($env:plain_prompt -ne "Y"))
{
    $prompt_existing = (Get-Command prompt).ScriptBlock
    if ($prompt_existing) {
        $prompt_current=(prompt)
        if ((!$prompt_current) -or ($prompt_current.length -lt 3)) {
            function global:prompt {"MVS> "}
        }
        elseif (! ($prompt_current.SubString(0, 3) -eq "GCC")) {
            $prompt_new = "`"GCC `" + " + $prompt_existing
            $def_fn_prompt_new = `
              "function global:prompt { " + $prompt_new + " }"
            Invoke-Expression $def_fn_prompt_new
        }
    }
    else {
        function global:prompt {"GCC> "}
    }
}

cd "$savedpwd"

Write-Host "SUCCESS: gcc.exe, mc.exe and ninja.exe set up"
