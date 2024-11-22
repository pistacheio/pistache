#!/usr/bin/env powershell

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Setup dev environment for Visual Studio

$savedpwd=$pwd

. $PSScriptRoot/helpers/commonsetup.ps1

if (Get-Command cl.exe -errorAction SilentlyContinue) {
    Write-Host "WARNING: MSVC's cl.exe already setup? Skipping MSVC prompt"
}
else {
    if (Test-Path -Path "$env:ProgramFiles\Microsoft Visual Studio") {
        cd "$env:ProgramFiles\Microsoft Visual Studio"
        $dirents=Get-ChildItem -Filter "????" -Name | `
          Sort-Object -Descending -Property Name
        foreach ($itm in $dirents) {
            if ("$itm" -match "^\d+$") {
                $itm_as_num=$itm/1
                if ($itm_as_num -gt 1970) {
                    if (Test-Path `
                      "$itm/Enterprise/Common7/Tools/Launch-VsDevShell.ps1") {
                          $launch_vs_dev_shell_dir = `
                            "$pwd\$itm\Enterprise\Common7\Tools"
                          break
                      }
                    elseif (Test-Path `
                     "$itm/Professional/Common7/Tools/Launch-VsDevShell.ps1") {
                         $launch_vs_dev_shell_dir = `
                           "$pwd\$itm\Professional\Common7\Tools"
                         break
                     }
                    elseif (Test-Path `
                      "$itm/Community/Common7/Tools/Launch-VsDevShell.ps1") {
                          $launch_vs_dev_shell_dir = `
                            "$pwd\$itm\Community\Common7\Tools"
                        break
                    }
                }
            }
        }
        if (! ($launch_vs_dev_shell_dir)) {
            $launch_vs_dev_shell_dir = `
              Get-ChildItem -Path "Launch-VsDevShell.ps1" -Recurse | `
              Sort-Object -Descending -Property LastWriteTime | `
              Select -Index 0 | Select -ExpandProperty "FullName" | Split-Path
        }
    }
    elseif (Test-Path -Path `
      "${env:ProgramFiles(x86)}\Microsoft Visual Studio") {
          cd "${env:ProgramFiles(x86)}\Microsoft Visual Studio"
          $dirents=Get-ChildItem -Filter "????" -Name | `
            Sort-Object -Descending -Property Name
          foreach ($itm in $dirents) {
              if ("$itm" -match "^\d+$") {
                  $itm_as_num=$itm/1
                  if ($itm_as_num -gt 1970) {
                      if (Test-Path `
                       "$itm/Enterprise/Common7/Tools/Launch-VsDevShell.ps1") {
                           $launch_vs_dev_shell_dir = `
                             "$pwd\$itm\Enterprise\Common7\Tools"
                           break
                       }
                      elseif (Test-Path "$itm/Professional/Common7/Tools/Launch-VsDevShell.ps1") {
                          $launch_vs_dev_shell_dir = `
                            "$pwd\$itm\Professional\Common7\Tools"
                          break
                      }
                      elseif (Test-Path `
                        "$itm/Community/Common7/Tools/Launch-VsDevShell.ps1") {
                            $launch_vs_dev_shell_dir = `
                              "$pwd\$itm\Community\Common7\Tools"
                            break
                        }
                  }
              }
          }
          if (! ($launch_vs_dev_shell_dir)) {
              $launch_vs_dev_shell_dir = `
                Get-ChildItem -Path "Launch-VsDevShell.ps1" -Recurse | `
                Sort-Object -Descending -Property LastWriteTime | `
                Select -Index 0 | Select -ExpandProperty "FullName" | `
                Split-Path
          }
      }
}

if (($launch_vs_dev_shell_dir) -And `
  (Test-Path -Path $launch_vs_dev_shell_dir)) {
      cd "$launch_vs_dev_shell_dir"
      # As at Aug-2024, VS 2022, Launch-VsDevShell.ps1 defaults to
      # 32-bit host and target architectures. However you can specify
      # both target architecture (-Arch option, valid values: x86,
      # amd64, arm, arm64) and host architecture (-HostArch option,
      # valid values: x86, amd64)
      #
      # If we don't do this, then meson will pickup 32-bit as the host
      # and target architecture. Meanwhile, vcpkg defaults to 64-bit
      # libraries (since we're on a 64-bit Windows). So then the
      # linker can't link, because it is trying to link 64-bit vcpkg
      # libs with our 32-bit object files.
      #
      # Ref: https://learn.microsoft.com/en-us/visualstudio/
      #          ide/reference/command-prompt-powershell?view=vs-2022
      try { ./Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64 }
      catch {
          cd "$launch_vs_dev_shell_dir"
          if (! ((./Launch-VsDevShell.ps1 -?) -like "*-HostArch*")) {
              Write-Host "Launch-VsDevShell.ps1 has no parameter -HostArch"
              if (Test-Path -Path "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe")
              {
                  Write-Host "Going to try Import-Module + Enter-VsDevShell"
                  cd `
                   "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer"
                  $vs_instance_id=.\vswhere.exe -property instanceId
                  cd "${env:ProgramFiles(x86)}/Microsoft Visual Studio"
                  $dirents=Get-ChildItem -Filter "????" -Name | `
                    Sort-Object -Descending -Property Name
                  foreach ($itm in $dirents) {
                      if ("$itm" -match "^\d+$") {
                          $itm_as_num=$itm/1
                          if ($itm_as_num -gt 1970) {
                              if (Test-Path "$itm/Enterprise/Common7/Tools/Microsoft.VisualStudio.DevShell.dll") {
                                  $dev_shell_path = `
                                    "$pwd\$itm\Enterprise\Common7\Tools"
                                  break
                              }
                              elseif (Test-Path "$itm/Professional/Common7/Tools/Microsoft.VisualStudio.DevShell.dll") {
                                  $dev_shell_path = `
                                    "$pwd\$itm\Professional\Common7\Tools"
                                  break
                              }
                              elseif (Test-Path "$itm/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll") {
                                  $dev_shell_path = `
                                    "$pwd\$itm\Community\Common7\Tools"
                                  break
                              }
                          }
                      }
                  }
                  if ("$dev_shell_path") {
                      $dev_shell_path = `
                        "$dev_shell_path\Microsoft.VisualStudio.DevShell.dll"
                  }
                  else {
                      Write-Host `
                        "Searching for Microsoft.VisualStudio.DevShell.dll"
                      $dev_shell_path = Get-ChildItem -Path `
                        "Microsoft.VisualStudio.DevShell.dll" -Recurse | `
                        Sort-Object -Descending -Property LastWriteTime | `
                        Select -Index 0 | `
                        Select -ExpandProperty "FullName"
                  }
                  if ($dev_shell_path) {
                      Import-Module "$dev_shell_path"
                      $env:VSCMD_DEBUG=1
                      Enter-VsDevShell -VsInstanceId $vs_instance_id `
                        -SkipAutomaticLocation -DevCmdDebugLevel Basic `
                        -DevCmdArguments '-arch=x64'
                  }
                  else {
                      throw `
                        "ERROR: No Microsoft.VisualStudio.DevShell.dll"
                  }
              }
              else {
                  throw "ERROR: vswhere.exe not found"
              }
          }
          else {
              throw "ERROR: Unexpected Launch-VsDevShell.ps1 error"
          }
      }
  }
else {
    throw("ERROR: Launch-VsDevShell.ps1 not found")
}

$env:CXX="cl"
$env:CC=$env:CXX

if (! (Get-Command ninja -errorAction SilentlyContinue)) {
    # Try looking in Microsoft Visual Studio

    if (Test-Path -Path "$env:ProgramFiles/Microsoft Visual Studio") {
        cd "$env:ProgramFiles/Microsoft Visual Studio"
        $ninja_dir = Get-ChildItem -Path "ninja.exe" -Recurse | `
          Sort-Object -Descending -Property LastWriteTime | `
          Select -Index 0 | Select -ExpandProperty "FullName" | Split-Path
    }

    if ((! ($ninja_dir)) -or (! (Test-Path -Path $ninja_dir))) {
        # Try looking in 32-bit Microsoft Visual Studio

        if (Test-Path -Path `
          "${env:ProgramFiles(x86)}/Microsoft Visual Studio") {
            cd "${env:ProgramFiles(x86)}/Microsoft Visual Studio"
            $ninja_dir = Get-ChildItem -Path "ninja.exe" -Recurse | `
              Sort-Object -Descending -Property LastWriteTime | `
              Select -Index 0 | Select -ExpandProperty "FullName" | Split-Path
        }
    }

    if (((! ($ninja_dir)) -or (! (Test-Path -Path $ninja_dir))) -and `
      (($env:VCPKG_DIR) -And (Test-Path -Path "$env:VCPKG_DIR\installed"))) {
          # Can't find ninja in Microsoft Visual Studio - look in vcpkg
          cd "$env:VCPKG_DIR\installed"

          $ninja_dir=Get-ChildItem -Path "ninja.exe" -Recurse | `
            Select -ExpandProperty "FullName" | Sort-Object { $_.Length } | `
            Select -Index 0 | Split-Path

          if ((! ($ninja_dir)) -or (! (Test-Path -Path $ninja_dir))) {
              if (Get-Command winget -errorAction SilentlyContinue) {
                  if (! (winget list "ninja")) {
                      winget install "Ninja-build.Ninja"
                      # Don't set ninja_dir - leaving it empty will
                      # mean it doesn't get added to $env:path below,
                      # which is good since winget takes care of the
                      # path for us
                  }
                  else {
                      Write-Host "WARNING: ninja already installed by winget, but not on path?"
                  }
              }
              else {
                  if (! (vcpkg list "vcpkg-tool-ninja")) {
                      vcpkg install vcpkg-tool-ninja
                      $ninja_dir=Get-ChildItem -Path "ninja.exe" -Recurse | `
                        Select -ExpandProperty "FullName" | `
                        Sort-Object { $_.Length } | Select -Index 0 | `
                        Split-Path
                  }
                  else {
                      Write-Host "WARNING: ninja already installed by vcpkg, but not found?"
                  }
              }
          }
      }

    if (($ninja_dir) -And (Test-Path -Path $ninja_dir)) {
        $env:Path="$ninja_dir;$env:Path" # Puts ninja.exe on the Path
    }

    cd ~
}


if ((! ($env:plain_prompt)) -or ($env:plain_prompt -ne "Y"))
{
    $prompt_existing = (Get-Command prompt).ScriptBlock
    if ($prompt_existing) {
        $prompt_current=(prompt)
        if ((!$prompt_current) -or ($prompt_current.length -lt 3)) {
            function global:prompt {"MVS> "}
        }
        elseif (! ($prompt_current.SubString(0, 3) -eq "MVS")) {
            $prompt_new = "`"MVS `" + " + $prompt_existing
            $def_fn_prompt_new = `
              "function global:prompt { " + $prompt_new + " }"
            Invoke-Expression $def_fn_prompt_new
        }
    }
    else {
        function global:prompt {"MVS> "}
    }
}

cd "$savedpwd"
