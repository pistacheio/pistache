# commonsetup.ps1

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Setup that is shared between MSVC or GCC

$savedpwd = $pwd

cd ~

# Note regarding throw. Just like a regular error, a throw sets
# $Error[0]. Of course, throw additional exits the script provided it
# is not caught by a "catch" statement; but even if the throw is
# caught, $Error will still have been set.
if ($rights_raised_for_cmd) {
    $err_len_lwr_bnd = 0
    foreach ($erritm in $Error) {
        $err_len_lwr_bnd = 1
        break
    }
    if ($err_len_lwr_bnd -gt 0) {
        # We put this into the Error array, so we know any error older
        # than this one is not relevant to us
        $Error[0] = "<PistachexxRemoved>"
    }
}

function pstPressKeyIfRaisedAndErrThenExit {
    if (($rights_raised_for_cmd) -and (! $pst_press_key_if_r_and_e_then_x)) {
        $pst_press_key_if_r_and_e_then_x = "Y"

        $spotted_important_error = ""
        $i = 0
        foreach ($erritm in $Error) {
            if ("$Error[$i]" -eq "<PistachexxRemoved>") { break }

            $invok_ln = ($Error[$i].InvocationInfo.Line)
            $i = ($i + 1)

            # Anything that has "-errorAction SilentlyContinue", notably
            #   Get-Command, Wait-Process, Get-ChildItem
            # Also, anything that redirects output to $null
            #   reg query, vcpgk list, python --version, python3 --version
            #     Despite appearances "reg query xxx" does not set an
            #     error for no xxx
            #     Nor do vcpkg list, vcpkg search, in case of no package

            if (! (("$invok_ln" -like "*Get-Command*") -or `
              ("$invok_ln" -like "*Wait-Process*") -or `
              ("$invok_ln" -like "*Get-ChildItem*") -or `
              ("$invok_ln" -like "*python --version*") -or `
              ("$invok_ln" -like "*python3 --version*"))) {
                  $spotted_important_error = "Y"
                  break
              }
        }

        if ("$spotted_important_error" -eq "Y") {
            Write-Host `
              "Script using Admin rights completed with possible errors" `
              -ForegroundColor Yellow
            Write-Host "Press any key to continue" -ForegroundColor Yellow
            $x = $host.ui.RawUI.ReadKey("NoEcho,IncludeKeyDown")
        }
        else {
            Write-Host `
              "Script using Admin rights completed and will exit soon"
            Write-Host `
              "PRESS ANY KEY to exit script using Admin rights without delay"
            $to_ctr = (20 * 9) # 9 seconds in 50ms increments
            while(!$Host.UI.RawUI.KeyAvailable -and ($to_ctr-- -gt 0)) {
                Start-Sleep -Milliseconds 50
                  if (($to_ctr % 20) -eq 19) {
                      Write-Host -NoNewline (($to_ctr + 1) / 20)
                      Write-Host -NoNewline "`r" # Go back to start of line
                  }
            }
        }
        Write-Host "Exiting from shell that has Admin rights"

        # Note - using the "[Environment]::" causes the shell to exit,
        # not just the script
        [Environment]::Exit(0)
    }
}

if (! ([Environment]::Is64BitProcess)) {
    Write-Host "This script currently requires a 64-bit PowerShell process"
    Write-Host "On 64-bit Windows, make sure to use regular PowerShell, NOT x86 PowerShell"
    throw "PowerShell process not 64 bit"
}

function pstDoCmdWithAdmin {
    param (
        [string]$PstCommand,
        [int]$PstTimeoutInSeconds
    )
        if ($PstTimeoutInSeconds -lt 0) {
            throw "Timeout is negative for cmd: $PstCommand"
        }

    try {
        if ($savedpwd) {$my_savedpwd = "$savedpwd"}
        else {$my_savedpwd = "$pwd"}

        $pst_command = `
          "cd `"$my_savedpwd`"; " + '$rights_raised_for_cmd=''Y''; ' + `
          "$PstCommand"

        # We use "Start-Process" so we can do "-verb RunAs", which provides
        # admin-level privileges, like doing "sudo" on Linux
        $modeproc = Start-Process -FilePath powershell.exe `
          -ArgumentList "-NoProfile", "-noexit", `
          "-WindowStyle", "Normal", `
          "-Command", "$pst_command" `
          -PassThru -verb RunAs

        if ($PstTimeoutInSeconds) {
            Write-Host `
              "Wait up to $PstTimeoutInSeconds seconds for cmd to complete"

            # keep track of timeout event
            $modetimeouted = $null

            $modeproc | Wait-Process -Timeout "$PstTimeoutInSeconds" `
              -ErrorAction SilentlyContinue -ErrorVariable modetimeouted
            Write-Host "cmd completed; continuing..."

            if ($modetimeouted) {
                $modeproc | kill
                Write-Warning "cmd timeout from: $PstCommand"
            }
            elseif ($modeproc.ExitCode -ne 0) {
                Write-Warning "cmd error from: $PstCommand"
            }
        }
        else { # Not really recommended
            Write-Host "Wait for cmd to complete, no timeout"
            $modeproc | Wait-Process -ErrorAction SilentlyContinue
            Write-Host "cmd completed; continuing..."
        }
    }
    catch {
        Write-Warning "cmd throw from: $PstCommand"
    }
}

$have_admin_rights = ([Security.Principal.WindowsPrincipal] `
  [Security.Principal.WindowsIdentity]::GetCurrent() `
  ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

# Things that require admin rights:
#   reg add/remove for HKLM
#   Any command the adds/delete content outside the user's own folders:
#       New-Item
#       Expand-Archive (cf. -DestinationPath)
#       Invoke-WebRequest (cf. -Outfile)
#       mkdir
#       cmake --install ... (and cmake may be invoked via a variable)
#       vs_community.exe --add ... (at least the first time)
#       xxx.sfx.exe (self extracting archive, cf. "-oXXX" parameter)
# Note: Generally, winget and vcpkg remove/install do NOT require admin rights
# However, somce specific packages do require admin rights to install/remove:
#       winget "Git.Git"

function pstRunScriptWithAdminRightsIfNotAlready {
    if ($have_admin_rights) { return $FALSE }
    if ($script_already_run_with_admin_rights) { return $FALSE }
    $script_already_run_with_admin_rights = $TRUE

    if ($pst_outer_ps_cmd) {
        $invocation_fp = $pst_outer_ps_cmd
    }
    else {
        ($my_parent_invocation = `
          (Get-Variable -Scope:1 -Name:MyInvocation -ValueOnly)) *> $null
        if ($my_parent_invocation) {
            $invocation_fp = $my_parent_invocation.PSCommandPath
        }
        if (! $invocation_fp) {
            $invocation_fp = $PSCommandPath
        }
    }

    Write-Host "To get Admin rights, relaunching cmd $invocation_fp"
    $invocation_fp = '& ''' + $invocation_fp + '''' # in case path has space

    pstDoCmdWithAdmin -PstTimeoutInSeconds 0 -PstCommand "$invocation_fp"
    return $TRUE
  }

# Git install needs to be the first thing in the script that might
# raise adin rights, so that, if git is installed by an admin-raised
# invoction of the script then, when control returns here to the
# non-admin-rights script here, we will know to exit the
# non-admin-rights script with the git-configuration advice.
if (! (Get-Command git -errorAction SilentlyContinue)) {

    if ((Test-Path -Path "$env:ProgramFiles/Git/cmd/git.exe") -Or `
      (Test-Path -Path "$env:ProgramFiles/Git/bin/git.exe")) {
          $git_dir = "$env:ProgramFiles/Git"
      }
    elseif ((Test-Path -Path "${env:ProgramFiles(x86)}/Git/cmd/git.exe") -Or `
      (Test-Path -Path "${env:ProgramFiles(x86)}/Git/bin/git.exe")) {
          $git_dir = "${env:ProgramFiles(x86)}/Git"
    }

    if (! $git_dir) {
        Write-Host "git not found; will attempt to install git"

        # Set $git_newly_installed _before_ we raise to admin
        # privilege, so (presuming we don't have the privileges
        # already) the advise about configuring git will appear in the
        # appear in the unprivileged shell
        $git_newly_installed = "y"

        # We get admin privilege here, even for winget - even though
        # winget does not not generally require admin privilege to do
        # an install, "Git.Git" is a special package that requires
        # admin rights for winget install
        if (! (pstRunScriptWithAdminRightsIfNotAlready)) {
            if (Get-Command winget -errorAction SilentlyContinue) {
                cd ~
                # Don't need admin privilege here, since OutFile is in
                # user's own folders
                Invoke-WebRequest -Uri `
                  "https://git.io/JXGyd" -OutFile "git.inf"
                winget install --accept-source-agreements "Git.Git" `
                  --override '/SILENT /LOADINF="git.inf"'

                # Although winget will have adjusted the path for us
                # already, that adjustment takes effect only after we have
                # started a new shell. So for the benefit of this shell,
                # since git is newly installed by winget, we add git to
                # the path
                if ((Test-Path -Path "$env:ProgramFiles/Git/cmd/git.exe") -Or `
                  (Test-Path -Path "$env:ProgramFiles/Git/bin/git.exe")) {
                      $git_dir = "$env:ProgramFiles/Git"
                  }
                elseif ((Test-Path -Path `
                  "${env:ProgramFiles(x86)}/Git/cmd/git.exe") -Or `
                  (Test-Path -Path `
                    "${env:ProgramFiles(x86)}/Git/bin/git.exe")) {
                      $git_dir = "${env:ProgramFiles(x86)}/Git"
                  }
            }
            else {
                $mingit_latest_url=(Invoke-WebRequest -Uri "https://raw.githubusercontent.com/git-for-windows/git-for-windows.github.io/refs/heads/main/latest-64-bit-mingit.url").Content
                Write-Host "Downloading mingit"
                Invoke-WebRequest -Uri "$mingit_latest_url" `
                  -Outfile "mingit_latest.zip"
                Expand-Archive -Path "mingit_latest.zip" `
                  -DestinationPath "$env:ProgramFiles/Git"
                $git_dir = "$env:ProgramFiles/Git"
            }
        }
        else {
            # We set git_dir here so it is added to path before we
            # call Exit below. Hence the user will able to invoke git
            # after the script has exited
            if ((Test-Path -Path "$env:ProgramFiles/Git/cmd/git.exe") -Or `
              (Test-Path -Path "$env:ProgramFiles/Git/bin/git.exe")) {
                  $git_dir = "$env:ProgramFiles/Git"
              }
            elseif ((`
              Test-Path -Path "${env:ProgramFiles(x86)}/Git/cmd/git.exe") -Or `
              (Test-Path -Path "${env:ProgramFiles(x86)}/Git/bin/git.exe")) {
                  $git_dir = "${env:ProgramFiles(x86)}/Git"
              }
        }
    }

    if ($git_dir -and `
        ((! (Get-Command winget -errorAction SilentlyContinue)) -or `
      (! (winget list "Git.Git")) -or ($git_newly_installed)))
        {
      $env:Path="$git_dir\cmd;$git_dir\mingw64\bin;$git_dir\usr\bin;$env:Path"
    }

    if ($git_newly_installed) {
        cd $savedpwd

        if (! $rights_raised_for_cmd)
        { # else we're running after elevating rights, so this
          # advisory message will appear later in the unprivileged shell; no
          # need to show it now in the privileged shell too
            Write-Host ' '
            Write-Host "git newly installed"
            Write-Host "You should configure git as well, e.g.:"
            Write-Host '  git config --global user.email "you@somedomain.com"'
            Write-Host '  git config --global user.name "Your Name"'
            Write-Host `
              '  git config --global core.editor "Your favorite editor"'
            Write-Host '  etc.'
            Write-Host `
              'Additionally, please configure git so it can access GitHub'
            Write-Host '  For instance, you might:'
            Write-Host '    Configure an access token per:'
            Write-Host '      https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/managing-your-personal-access-tokens'
            Write-Host `
              '    Install "gh" per: https://github.com/cli/cli#windows'
            Write-Host `
              '    On command line, setup git to use your access token:'
            Write-Host '      gh auth login'
            Write-Host '      (then restart your shell prompt)'
            Write-Host ' '
            Write-Host "EXIT: Configure git and github, then rerun this script"
        }

        $pst_stop_running = "Y"
        pstPressKeyIfRaisedAndErrThenExit

        # Note - we're calling exit here in case we are in an
        # unprivileged shell, in which case
        # pstPressKeyIfRaisedAndErrThenExit will do nothing
        Exit(0) # Exit the script, but not the shell (no "[Environment]::")
    }
}

# Enable developer mode, if not enabled already
if (Get-Command reg -errorAction SilentlyContinue) {
    ($app_model_unlock_there = (reg query "HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock")) *> $null

    if ((! $app_model_unlock_there) -or `
      (! ((Get-Item "hklm:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock").Property -contains "AllowDevelopmentWithoutDevLicense")) -or `
      (((Get-ItemProperty -Path "hklm:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock" -Name AllowDevelopmentWithoutDevLicense).AllowDevelopmentWithoutDevLicense) -ne 1))
        {
        if (! (pstRunScriptWithAdminRightsIfNotAlready)) {
            try {
                # Note: "reg add" sets value even if property already exists
                reg add "HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock" `
                  /t REG_DWORD /f /v "AllowDevelopmentWithoutDevLicense" /d "1"
                if (! ($?)) {
                    Write-Warning "developer mode enablement error"
                }
            }
            catch {
                Write-Warning "developer mode enablement throw"
            }
        }
    }
}

if (Test-Path -Path "$env:HOMEDRIVE\vcpkg\vcpkg.exe") {
    $env:VCPKG_DIR="$env:HOMEDRIVE\vcpkg"
}
elseif (Test-Path -Path "$env:SYSTEMDRIVE\vcpkg\vcpkg.exe") {
    $env:VCPKG_DIR="$env:SYSTEMDRIVE\vcpkg"
}
elseif (Test-Path -Path "$env:USERPROFILE\vcpkg\vcpkg.exe") {
    $env:VCPKG_DIR="$env:USERPROFILE\vcpkg"
}
elseif (Test-Path -Path "$env:USERPROFILE\progsloc\vcpkg\vcpkg.exe") {
    $env:VCPKG_DIR="$env:USERPROFILE\progsloc\vcpkg"
}
elseif (Test-Path -Path "$env:USERPROFILE\progs\vcpkg\vcpkg.exe") {
    $env:VCPKG_DIR="$env:USERPROFILE\progs\vcpkg"
}
elseif (Test-Path -Path "$env:USERPROFILE\programs\vcpkg\vcpkg.exe") {
    $env:VCPKG_DIR="$env:USERPROFILE\programs\vcpkg"
}
else {
    Write-Host "Looking for vcpkg (may take some time)"

    # Search the user's directory
    cd "~"
    # Note (@Jan/2025): We exclude "Microsoft Visual Studio" because
    # if we use the version of vcpkg that comes with Visual Studio
    # then, when we invoke vcpkg install ..., we get an error "This
    # vcpkg distribution does not have a classic mode instance".
    $vcpkg_exe_path = Get-ChildItem -Path "." -Dir -Recurse `
      -ErrorAction SilentlyContinue | `
      Where-Object {$_.FullName -notLike '*Microsoft Visual Studio*'} | `
      Get-ChildItem -Filter "vcpkg.exe" -File `
      -ErrorAction SilentlyContinue | `
      Sort-Object -Descending -Property LastWriteTime | `
      Select -Index 0 | Select -ExpandProperty "FullName" | Split-Path
    if (($vcpkg_exe_path) -And (Test-Path -Path $vcpkg_exe_path)) {
        $env:VCPKG_DIR=$vcpkg_exe_path
    }
    else {
        # Search system drive, ugh
        cd "$env:SYSTEMDRIVE\"

        $vcpkg_exe_path = Get-ChildItem -Path "." -Dir -Recurse `
          -ErrorAction SilentlyContinue | `
          Where-Object {$_.FullName -notLike '*Microsoft Visual Studio*'} | `
          Get-ChildItem -Filter "vcpkg.exe" -File `
          -ErrorAction SilentlyContinue | `
          Sort-Object -Descending -Property LastWriteTime | `
          Select -Index 0 | Select -ExpandProperty "FullName" | Split-Path
        if (($vcpkg_exe_path) -And (Test-Path -Path $vcpkg_exe_path)) {
            $env:VCPKG_DIR=$vcpkg_exe_path
        }
    }
    if (! ($env:VCPKG_DIR)) {
        # -cne is case-insensitive string compare
        if ($env:HOMEDRIVE -cne $env:SYSTEMDRIVE) {
            # Search home drive, also ugh
            cd "$env:HOMEDRIVE\"

            $vcpkg_exe_path = Get-ChildItem -Path "." -Dir -Recurse `
              -ErrorAction SilentlyContinue | `
              Where-Object `
                {$_.FullName -notLike '*Microsoft Visual Studio*'} | `
              Get-ChildItem -Filter "vcpkg.exe" -File `
              -ErrorAction SilentlyContinue | `
              Sort-Object -Descending -Property LastWriteTime | `
              Select -Index 0 | Select -ExpandProperty "FullName" | Split-Path
            if (($vcpkg_exe_path) -And (Test-Path -Path $vcpkg_exe_path)) {
                $env:VCPKG_DIR=$vcpkg_exe_path
	    }
        }
        else {
            Write-Host "vcpkg.exe not found"
        }
    }
}

if ((! ($env:VCPKG_DIR)) -Or (! (Test-Path -Path "$env:VCPKG_DIR"))) {
    $vcpkg_new_inst_dir = $env:VCPKG_DIR
    if (! ($vcpkg_new_inst_dir)) {
        $vcpkg_new_inst_dir = "$env:SYSTEMDRIVE\vcpkg"
    }
    Write-Host "Will attempt to install VCPKG to $vcpkg_new_inst_dir"

    # We get admin privilege here, so we can write to the root of the
    # system drive with mkdir
    if (! (pstRunScriptWithAdminRightsIfNotAlready)) {

        if (! (Test-Path -Path "$vcpkg_new_inst_dir")) {
            mkdir "$vcpkg_new_inst_dir"
            cd "$vcpkg_new_inst_dir"
            cd ..
        }

        git clone https://github.com/Microsoft/vcpkg.git
        if (! ($?)) {
            throw `
              "FAILED: git clone vcpkg. Possible github authentication issue"
        }
        cd vcpkg
        .\bootstrap-vcpkg.bat
        cd "$vcpkg_new_inst_dir" # In case bootstrap-vcpkg.bat changed dirc
        .\vcpkg.exe integrate install
    }
    $env:VCPKG_DIR = $vcpkg_new_inst_dir
}

if (($env:VCPKG_DIR) -And (Test-Path -Path "$env:VCPKG_DIR")) {
    $env:VCPKG_INSTALLATION_ROOT=$env:VCPKG_DIR # As per github Windows images
    $env:VCPKG_ROOT=$env:VCPKG_INSTALLATION_ROOT
    $env:Path="$env:Path;$env:VCPKG_DIR"

    $my_vcpkg_pc_dir = "$env:VCPKG_DIR\installed\x64-windows\lib\pkgconfig"
    if ($env:PKG_CONFIG_PATH) {
        if (! (($Env:PKG_CONFIG_PATH -split ";").TrimEnd('\').TrimEnd('/') `
          -contains $my_vcpkg_pc_dir)) {
              $env:PKG_CONFIG_PATH = "$env:PKG_CONFIG_PATH;$my_vcpkg_pc_dir"
          }
    }
    else {
        $env:PKG_CONFIG_PATH="$my_vcpkg_pc_dir"
    }
}

cd $savedpwd

# We want Visual Studio to be installed even if we're not going to use
# the Visual Studio compiler, so we can have access to the Windows
# SDK(s) that are installed along with Visual Studio
if (! (((Test-Path -Path "$env:ProgramFiles\Microsoft Visual Studio") -and `
       ((Get-ChildItem -Path "$env:ProgramFiles\Microsoft Visual Studio" `
           -Include "*.exe" -Recurse) | Select -ExpandProperty "FullName" | `
  where {! ($_ -like '*\Installer\*')} | Select-Object -First 1)) -or `
  ((Test-Path -Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio") -and `
  ((Get-ChildItem -Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio" `
      -Include "*.exe" -Recurse) | Select -ExpandProperty "FullName" | `
  where {! ($_ -like '*\Installer\*')} | Select-Object -First 1)))) {
      # Note - the use of Get-ChildItem, looking for "*.exe", here, is
      # to ensure that the "Microsoft Visual Studio" folder actually
      # contains something meaningful; for instance, if Visual Studio
      # 2022 is uninstalled, an empty $env:ProgramFiles\Microsoft
      # Visual Studio\2022 folder is left behind, and a
      # ${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022
      # containing only the installer is left behind, and we don't
      # want that to fool us into thinking VS 2022 is installed.
      # Note also that the "Select-Object -First 1" stops the
      # Get-ChildItem search once the first exe outside
      # '*\Installer\*' is found.

      Write-Host "No existing Visual Studio detected"

      # We do pstRunScriptWithAdminRightsIfNotAlready here, since
      # we'll need admin privilege when we run the Visual Studio
      # installer below
      if (! (pstRunScriptWithAdminRightsIfNotAlready)) {
          # Note, to uninstall Visual Studio, use the Visual Studio
          # installer. In the UI, search for "Installer". On command
          # line, it is likely called vs_installer.exe; may be found in
          # C:\Program Files (x86)\Microsoft Visual Studio\Installer
          # In the Installer, click on More (likely RHS), then Uninstall.

          $vsyr="2022"
          try {
              Write-Host "Looking for current Visual Studio major release"
              $wiki_links = (Invoke-WebRequest -Uri `
                "https://en.wikipedia.org/wiki/Visual_Studio").Links
              foreach ($itm in $wiki_links) {
                  $href = $itm.href
                  if ($href -like `
                    "https://learn.microsoft.com/en-us/visualstudio/releases/2*") {
                        $foundvsyr = $href.Substring(56, 4)
                        if (($foundvsyr/1) -ge ($vsyr/1)) {
                            $vsyr_was_found = $foundvsyr
                            $vsyr = $foundvsyr
                        }
                    }
              }
          }
          catch {}
          if (! ($vsyr_was_found))
          {
              Write-Host "VS major release not determined, defaulting to $vsyr"
          }

          try {
              Write-Host "Fetching link to Visual Studio $vsyr"
              $releases_url = -join("https://learn.microsoft.com/en-us/visualstudio/releases/", $vsyr, "/release-history#release-dates-and-build-numbers")
              $links = (Invoke-WebRequest -Uri $releases_url).Links
              foreach ($itm in $links) {
                  $href = $itm.href
                  if ($href -like "*vs_community.exe*") {
                      Write-Host `
                        "Fetching Visual Studio $vsyr vs_community.exe"
                      cd ~
                      # Don't need admin privilege here, since OutFile is in
                      # user's own folders
                      Invoke-WebRequest -Uri $href -OutFile "vs_community.exe"
                      break
                  }
              }
          }
          catch {}

          if (! (Test-Path -Path "vs_community.exe")) {
              $vs_uri = "https://aka.ms/vs/17/release/vs_community.exe"
              Write-Host `
                "vs_community.exe not found, attempting download from $vs_uri"
              Invoke-WebRequest -Uri $vs_uri -OutFile "vs_community.exe"
          }

          Write-Host "This script has downloaded vs_community.exe"
          Write-Host `
            "Running Visual Studio installer; will take time - many minutes..."

          # Already did pstRunScriptWithAdminRightsIfNotAlready above
          # We have to use Start-Process here so we can do "wait",
          # i.e. wait until process complete before continuing
          # Ref: https://learn.microsoft.com/en-us/visualstudio/install/command-line-parameter-examples?view=vs-2022#using---wait
          # Also: "--quiet" makes installer run without UI
          #       "--force" is needed to avoid an error on Windows 10
          #       (it appears the installer starts a Visual Studio component,
          #        then won't continue without confirmation that it can close
          #        that component)
          $vsin_proc = Start-Process -FilePath vs_community.exe -ArgumentList `
            "--add", "Microsoft.VisualStudio.Workload.NativeDesktop", `
            "--includeRecommended", "--force", "--quiet", "--wait" `
            -Wait -PassThru
          if ($vsin_proc.ExitCode -ne 0) {
              Write-Error "Visual Studio install returned non-zero exit code"
          }
      }
  }

if (Test-Path -Path "$env:ProgramFiles\Microsoft Visual Studio") {
    $my_vs_path = "$env:ProgramFiles\Microsoft Visual Studio"
}
elseif (Test-Path -Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio") {
    $my_vs_path = "${env:ProgramFiles(x86)}\Microsoft Visual Studio"
}
else {
    Write-Error "ERROR: Visual Studio not installed as expected."
    Write-Error "If this happens while Visual Studio is still installing,"
    Write-Error "just complete the Visual Studio installation,"
    Write_Error "and then rerun this script"
    throw "ERROR: Visual Studio not installed as expected"
}

# We want to find cmake.exe in Visual Studio. To speed this up, we try
# and make some educated guesses as to cmake's location. If that
# doesn't work, we do a more general search.
cd "$my_vs_path"
$dirents=Get-ChildItem -Filter "????" -Name | `
  Sort-Object -Descending -Property Name
foreach ($itm in $dirents) {
    if ("$itm" -match "^\d+$") {
        $itm_as_num=$itm/1
        if ($itm_as_num -gt 1970) {
            if (! $my_vs_path_was_updated) {
                $my_vs_path = "$my_vs_path\$itm" # Only for first dirent
                $my_vs_path_was_updated = "Y"
            }

            if (Test-Path `
              "$itm\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe") {
                  $my_cmake_vs_edition = "Enterprise"
                  break
              }
            elseif (Test-Path `
              "$itm\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe") {
                  $my_cmake_vs_edition = "Professional"
                  break
              }
            elseif (Test-Path `
              "$itm\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe") {
                  $my_cmake_vs_edition = "Community"
                  break
              }
        }
    }
}
if ($my_cmake_vs_edition) {
    $my_cmake_fullpath = "$my_vs_path\$my_cmake_vs_edition\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}
else {
    cd "$my_vs_path"

    $my_cmake_fullpath = Get-ChildItem -Path "cmake.exe" -Recurse `
      -ErrorAction SilentlyContinue| `
      Sort-Object -Descending -Property LastWriteTime | `
      Select -Index 0 | Select -ExpandProperty "FullName"
    if ((! $my_cmake_fullpath) -or (! (Test-Path -Path $my_cmake_fullpath))) {
        throw "ERROR: cmake not found in Visual Studio"
    }
}

# pkg-config
if ((! (Get-Command pkg-config -errorAction SilentlyContinue)) -or `
  ((where.exe pkg-config) -like "*Strawberry*")) {
      # meson does not accept the version of pkg-config in Strawberry Perl

      if (Get-Command winget -errorAction SilentlyContinue)
      { # winget exists on newer versions of Windows 10 and on
        # Windows 11 and after. It is is not supported on Windows
        # Server 2019, and is experimental only on Windows Server
        # 2022.

          # Note: winget install bloodrock.pkg-config-lite' seemed to
          # work around Sept. 2024 but failed Dec 2024.
          # We use "$tmp =" to hide the ""No package found matching
          # input criteria" message that otherwise shows up from
          # winget
          $tmp = winget install --accept-source-agreements bloodrock.pkg-config-lite
          $winget_bloodrock_p_conf = $?
      }
      if (! $winget_bloodrock_p_conf)
      {
          # First, check pkgconfig installed, and install if not

          if (($env:VCPKG_DIR) -And `
            (Test-Path -Path "$env:VCPKG_DIR\installed")) {
                # Note "$env:VCPKG_DIR\installed" directory may not
                # exist if nothing has been installed in vcpkg yet
                cd "$env:VCPKG_DIR\installed"
                $my_vcpkg_pkgconfig_dir =
                Get-ChildItem -Path "pkgconfig" -Recurse | `
                  Sort-Object -Descending -Property LastWriteTime | `
                  Select -Index 0 | Select -ExpandProperty "FullName"
            }
          if ((! ($my_vcpkg_pkgconfig_dir)) -Or `
            (! (Test-Path -Path $my_vcpkg_pkgconfig_dir))) {
                Write-Host "Installing pkgconf with vcpkg"
                vcpkg install pkgconf
                cd "$env:VCPKG_DIR\installed"
                $my_vcpkg_pkgconfig_dir = `
                  Get-ChildItem -Path "pkgconfig" -Recurse | `
                  Sort-Object -Descending -Property LastWriteTime | `
                  Select -Index 0 | Select -ExpandProperty "FullName"
            }
          if ((! $my_vcpkg_pkgconfig_dir) -Or `
            (! (Test-Path -Path $my_vcpkg_pkgconfig_dir))) {
              throw "ERROR: pkgconf not installed as expected"
          }
      }
  }

($brotli_there = (vcpkg list "brotli")) *> $null
($zstd_there = (vcpkg list "zstd")) *> $null
if (! $brotli_there) { vcpkg install brotli }
if (! $zstd_there) { vcpkg install zstd }

if (($env:VCPKG_DIR) -And (Test-Path -Path "$env:VCPKG_DIR\installed")) {
    cd "$env:VCPKG_DIR\installed"

    # We add the ...\vcpkg\installed\...\bin directory to the
    # path. Not only does this mean that executables can be executed
    # from that directory, it also allows DLLS to be loaded from that
    # bin directory (since Windows DLL loader checks path), notably
    # event_core.dll from libevent.

    # Find ...\installed\...\bin directory. Choose shortest path
    # if more than one
    $vcpkg_installed_bin_dir=Get-ChildItem -Path "bin" -Recurse | `
      Select -ExpandProperty "FullName" | Sort-Object { $_.Length } | `
      Select -Index 0
    if (($vcpkg_installed_bin_dir) -And `
      (Test-Path -Path $vcpkg_installed_bin_dir)) {
          $env:Path="$env:Path;$vcpkg_installed_bin_dir"
      }
    else {
        throw "...\vcpkg\installed\...\bin directory not found"
    }

    $vcpkg_installed_pkgconf_dir=Get-ChildItem -Path "pkgconf.exe" -Recurse | `
      Select -ExpandProperty "FullName" | Sort-Object { $_.Length } | `
      Select -Index 0 | Split-Path
    if (($vcpkg_installed_pkgconf_dir) -And `
      (Test-Path -Path $vcpkg_installed_pkgconf_dir)) {
          $env:Path="$vcpkg_installed_pkgconf_dir;$env:Path"
          # Puts pkgconf.exe on the Path

          if ((! (Get-Command pkg-config.exe -errorAction SilentlyContinue)) `
            -or ((where.exe pkg-config) -like "*Strawberry*"))
          {
              if (! (pstRunScriptWithAdminRightsIfNotAlready)) {
                  # Requires admin privileges
                  New-Item -ItemType SymbolicLink `
                    -Path "$vcpkg_installed_pkgconf_dir\pkg-config.exe" `
                    -Target "$vcpkg_installed_pkgconf_dir\pkgconf.exe"
              }
          }
      }
}
else {
    throw "$env:VCPKG_DIR\installed not found"
}

if (Get-Command python -errorAction SilentlyContinue) {
    # Note that more recent versions of Windows treat python (and
    # python3) as special commands that always exists. If python is
    # invoked when it is not installed, Windows outputs a helpful
    # message on how to install it via app store. Consequently,
    # "Get-Command python" is not a sufficient test of whether python
    # is installed and available. We invoke python directly as an
    # additional test.
    ($python_there = (python --version)) *> $null
}
else {
    $python_there = $FALSE
}
if (Get-Command python3 -errorAction SilentlyContinue) {
    ($python3_there = (python3 --version)) *> $null
}
else {
    $python3_there = $FALSE
}

if (! $python3_there) {
      if ((! $python_there) -or `
        (! (((python --version).SubString(7, 1)/1) -ge 3))) {
            if (Test-Path -Path "$env:VCPKG_DIR\installed\x64-windows\tools\python3\python.exe") {
                $py3_path = "$env:VCPKG_DIR\installed\x64-windows\tools\python3"
            }
            elseif (($env:VCPKG_DIR) -And `
              (Test-Path -Path "$env:VCPKG_DIR\installed")) {
                  cd "$env:VCPKG_DIR\installed"
                  $py3_path = Get-ChildItem -Path "python.exe" -Recurse | `
                    Sort-Object -Descending -Property LastWriteTime | `
                    Select -Index 0 | Select -ExpandProperty "FullName" | `
                    Split-Path
              }
            ($py3_vcpkg_there = (vcpkg list "py3_vcpkg")) *> $null
            if ((! ($py3_path)) -and (! $py3_vcpkg_there)) {
                vcpkg install python3

                if (Test-Path -Path "$env:VCPKG_DIR\installed\x64-windows\tools\python3\python.exe") {
                    $py3_path = "$env:VCPKG_DIR\installed\x64-windows\tools\python3"
                }
                elseif (($env:VCPKG_DIR) -And `
                  (Test-Path -Path "$env:VCPKG_DIR\installed")) {
                      cd "$env:VCPKG_DIR\installed"
                      $py3_path = Get-ChildItem -Path "python.exe" -Recurse | `
                        Sort-Object -Descending -Property LastWriteTime | `
                        Select -Index 0 | Select -ExpandProperty "FullName" |
                        Split-Path
                  }
            }
            if ($py3_path) {
                $env:Path="$py3_path;$py3_path\Scripts;$env:Path"
            }
            else {
                throw "Python3's python.exe not found"
            }
        }
}

if (! (Get-Command pip3 -errorAction SilentlyContinue)) {
    # I guess this doesn't require admin rights?
    python -m ensurepip
}

function Find-Meson-Exe {
    # Note: Re: the "-Exclude "*appleveltile*" used below, this is a
    # workaround to deal with a problem Get-ChildItem (and other
    # commands) have with very long paths. Dec/2024, installing on
    # Windows 11 2H24, we saw in AppData a path:
    #   C:\Users\<name>\AppData\Local\Microsoft\Windows\CloudStore\{c4a835c8-c76d-487f-92fd-425b3dccfb5d}\windows.data.apps.appleveltileinfo\appleveltilelist\w~{7c5a40ef-a0fb-4bfc-874a-c0f2e0b9fa8e}windows kits10shortcutswindowsstoreappdevcentertoolsdocumentation.url
    # This path is so long it causes an IO error (read error) for
    # Get-ChildItem, so we exclude its parent from the search
    if (Test-Path "~/AppData/Local/Packages") {
        cd "~/AppData/Local/Packages"
        $meson_exe_path = Get-ChildItem -Path "meson.exe" -Recurse `
          -Exclude "*appleveltile*" | `
          Sort-Object -Descending -Property LastWriteTime | `
          Select -Index 0 | Select -ExpandProperty "FullName" | Split-Path
    }

    if (((! ($meson_exe_path)) -or (! (Test-Path -Path $meson_exe_path))) -and (Test-Path "~/AppData/Roaming/Python")) {
        cd "~/AppData/Roaming/Python"
        $meson_exe_path = Get-ChildItem -Path "meson.exe" -Recurse `
          -Exclude "*appleveltile*" | `
          Sort-Object -Descending -Property LastWriteTime | `
          Select -Index 0 | Select -ExpandProperty "FullName" | Split-Path
    }

    if (((! ($meson_exe_path)) -or (! (Test-Path -Path $meson_exe_path))) -and (Test-Path "~/AppData")) {
        cd "~/AppData"
        $meson_exe_path = Get-ChildItem -Path "meson.exe" -Recurse `
          -Exclude "*appleveltile*" | `
          Sort-Object -Descending -Property LastWriteTime | `
          Select -Index 0 | Select -ExpandProperty "FullName" | Split-Path
    }

    return $meson_exe_path
}

if (! (Get-Command meson -errorAction SilentlyContinue)) {
    $meson_exe_path = Find-Meson-Exe
    if ((! ($meson_exe_path)) -Or (! (Test-Path -Path $meson_exe_path))) {
        Write-Host "Installing meson with pip3"
        pip3 install --user meson --no-warn-script-location
        $meson_exe_path = Find-Meson-Exe
    }

    if (($meson_exe_path) -And (Test-Path -Path $meson_exe_path)) {
        $env:Path="$env:Path;$meson_exe_path"
    }
    else {
        throw "ERROR: meson.exe directory not added to path"
    }
}

($curl_openssl_there = (vcpkg list "curl[openssl]")) *> $null
if (! $curl_openssl_there) { # vcpkg list - list installed packages
    ($curl_there = (vcpkg list "curl")) *> $null
    if ($curl_there) {
        vcpkg remove curl
    }
    vcpkg install curl[openssl]
}

($openssl_there = (vcpkg list "openssl")) *> $null
($libevent_there = (vcpkg list "libevent")) *> $null
if (! $openssl_there) { vcpkg install openssl }
if (! $libevent_there) { vcpkg install libevent }

if ((! (Get-ChildItem -Path "$env:ProgramFiles\googletest*" `
  -ErrorAction SilentlyContinue)) -And `
  (! (Get-ChildItem -Path "${env:ProgramFiles(x86)}\googletest*" `
  -ErrorAction SilentlyContinue))) {
      # We do pstRunScriptWithAdminRightsIfNotAlready so we have admin
      # privilege for "cmake --install ..." below
      if (! (pstRunScriptWithAdminRightsIfNotAlready)) {
          cd ~
          if (! (Test-Path -Path "googletest")) {
              git clone https://github.com/google/googletest.git
              if (! ($?)) {
                  throw `
                    "FAILED: git clone googletest. Possible github auth issue"
              }
          }
          cd googletest
          if (! (Test-Path -Path "build")) { mkdir build }
          cd build
          & "$my_cmake_fullpath" ..
          & "$my_cmake_fullpath" --build .
          & "$my_cmake_fullpath" --install . --config Debug
      }
  }

($date_there = (vcpkg list "date")) *> $null
if (! $date_there) { vcpkg install date } #Howard-Hinnant-Date

if ((! (Get-ChildItem -Path "$env:ProgramFiles\zlib*" `
  -ErrorAction SilentlyContinue)) -And `
  (! (Get-ChildItem -Path "${env:ProgramFiles(x86)}\zlib*" `
  -ErrorAction SilentlyContinue))) {
      # We do pstRunScriptWithAdminRightsIfNotAlready so we have admin
      # privilege for "cmake --install ..." below
      if (! (pstRunScriptWithAdminRightsIfNotAlready)) {
          Invoke-WebRequest -Uri https://zlib.net/current/zlib.tar.gz -OutFile zlib.tar.gz
          tar -xvzf .\zlib.tar.gz
          cd "zlib*" # Adjusts to whatever we downloaded, e.g. zlib-1.3.1
          if (! (Test-Path -Path "build")) { mkdir build }
          cd build
          & "$my_cmake_fullpath" ..
          & "$my_cmake_fullpath" --build . --config Release
          & "$my_cmake_fullpath" --install . --config Release
      }
  }

if (! (Get-Command doxygen -errorAction SilentlyContinue)) {
    if (Test-Path -Path "$env:USERPROFILE\doxygen.bin") {
        $env:Path="$env:Path;$env:USERPROFILE\doxygen.bin"
    }
    elseif (Test-Path -Path "$env:ProgramFiles\doxygen\bin\doxygen.exe") {
        # This could happen if doxygen were installed in an admin
        # shell while we ran here in a non-admin shell - doxygen is
        # installed, but still needs to be added to our path here
        $env:Path="$env:Path;$env:ProgramFiles\doxygen\bin"
    }
    elseif (Test-Path -Path `
      "${env:ProgramFiles(x86)}\doxygen\bin\doxygen.exe") {
          # This could happen if doxygen were installed in an admin
          # shell while we ran here in a non-admin shell - doxygen is
          # installed, but still needs to be added to our path here
          $env:Path="$env:Path;${env:ProgramFiles(x86)}\doxygen\bin"
    }
    else {
        if (Get-Command winget -errorAction SilentlyContinue)
        {
            if (! (pstRunScriptWithAdminRightsIfNotAlready)) {
                winget install --accept-source-agreements doxygen
            }

            # Although winget will have adjusted the path for us
            # already, that adjustment takes effect only after we have
            # started a new shell. So for the benefit of this shell,
            # since doxygen is newly installed by winget, we add it to
            # the path
            if (Test-Path -Path "$env:ProgramFiles\doxygen\bin\doxygen.exe") {
                  $doxygen_dir = "$env:ProgramFiles\doxygen\bin"
              }
            elseif (Test-Path -Path `
              "${env:ProgramFiles(x86)}\doxygen\bin\doxygen.exe") {
                  $doxygen_dir = "${env:ProgramFiles(x86)}\doxygen\bin"
              }
            if ($doxygen_dir) {
                $env:Path="$env:Path;$doxygen_dir"
            }
            else {
                Write-Warning "doxygen not found where expected"
            }
        }
        else
        {
            try {
                cd ~
                Write-Host "doxygen: Fetching version number of latest release"
                $doxygen_latest_links = (Invoke-WebRequest -Uri "https://github.com/doxygen/doxygen/releases/latest").Links
                foreach ($itm in $doxygen_latest_links) {
                    $ot = $itm.outerText
                    if ($ot -like "Release_*") {
                        $ot_ver_with_underbars = $ot.Substring(8)
                        $ot_ver_with_dots = `
                          $ot_ver_with_underbars -replace "_", "."
                        $download_uri = `
                          -join("https://www.doxygen.nl/files/doxygen-", `
                          $ot_ver_with_dots.TrimEnd(), ".windows.x64.bin.zip")
                        Write-Host "doxygen: Fetching $download_uri"
                        Invoke-WebRequest -Uri $download_uri `
                          -OutFile doxygen.bin.zip
                        break
                    }
                }
            }
            catch {
            }
            if (! (Test-Path -Path "doxygen.bin.zip")) {
                Write-Host "Failed to download latest doxygen.bin.zip"
                $download_uri = "https://www.doxygen.nl/files/doxygen-1.12.0.windows.x64.bin.zip"
                Write-Host "Fetching $download_uri"
                Invoke-WebRequest -Uri $download_uri -OutFile doxygen.bin.zip
            }
            try {
                # Don't need admin privilege here, expanding to user's
                # own fodlers
                Expand-Archive doxygen.bin.zip -DestinationPath doxygen.bin
            }
            catch {
                if ($download_uri) {
                    # Occasionally (1 time in 100?) Expand-Archive
                    # will fail, with an error like:
                    #   New-Object : Exception calling ".ctor" with "3"...
                    # Apparently, this happens when the downloaded zip
                    # file was corrupt. We try downloading it again;
                    # and apparently setting a different UserAgent may
                    # help.
                    Invoke-WebRequest -Uri $download_uri `
                      -OutFile doxygen.bin.zip -UserAgent "NativeHost"
                    Expand-Archive doxygen.bin.zip -DestinationPath doxygen.bin
                }
                else {
                    throw "Unknown $download_uri for doxygen"
                }
            }
            $env:Path="$env:Path;$env:USERPROFILE\doxygen.bin"
        }
    }
}

$pst_username = `
  [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
if (-not (Test-Path -Path "$env:ProgramFiles\pistache_distribution")) {
    if (! (pstRunScriptWithAdminRightsIfNotAlready)) {
        mkdir "$env:ProgramFiles\pistache_distribution"

        # Grant write rights  ("FullControl") to the current user
        $pst_acl = Get-Acl "$env:ProgramFiles\pistache_distribution"
        $pst_ar = `
          New-Object System.Security.AccessControl.FileSystemAccessRule( `
          "$pst_username", "FullControl", "ContainerInherit,ObjectInherit", `
          "None", "Allow")
        $pst_acl.SetAccessRule($pst_ar)
        Set-Acl "$env:ProgramFiles\pistache_distribution" $pst_acl
    }
}
else {
    $pst_acl = Get-Acl "$env:ProgramFiles\pistache_distribution"
    $pst_permission = $pst_acl.Access | ?{$_.IdentityReference -like "$pst_username"} | ?{$_.AccessControlType -like "Allow"}  | ?{$_.FileSystemRights -like "FullControl"}
    if (! $pst_permission) {
        if (! (pstRunScriptWithAdminRightsIfNotAlready)) {
            # Grant write rights ("FullControl") to the current user
            $pst_ar = `
              New-Object System.Security.AccessControl.FileSystemAccessRule( `
              "$pst_username", "FullControl", `
              "ContainerInherit,ObjectInherit", `
              "None", "Allow")
            $pst_acl.SetAccessRule($pst_ar)
            Set-Acl "$env:ProgramFiles\pistache_distribution" $pst_acl
        }
    }
}


cd $savedpwd
