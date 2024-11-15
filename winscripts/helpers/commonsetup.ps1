# commonsetup.ps1

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Setup that is shared between MSVC or GCC

$savedpwd = $pwd

cd ~

if (! (Get-Command git -errorAction SilentlyContinue)) {

    if ((Test-Path -Path "$env:ProgramFiles/Git/cmd/git.exe") -Or `
      (Test-Path -Path "$env:ProgramFiles/Git/bin/git.exe")) {
          $git_dir = "$env:ProgramFiles/Git"
      }
    elseif ((Test-Path -Path "${env:ProgramFiles(x86)}/Git/cmd/git.exe") -Or `
      (Test-Path -Path "${env:ProgramFiles(x86)}/Git/bin/git.exe")) {
          $git_dir = "${env:ProgramFiles(x86)}/Git"
    }

    if (! ($git_dir)) {
        Write-Host "git not found; will attempt to install git"
        if (Get-Command winget -errorAction SilentlyContinue) {
            Invoke-WebRequest -Uri "https://git.io/JXGyd" -OutFile "git.inf"
            winget install Git.Git --override '/SILENT /LOADINF="git.inf"'
            # Don't set $git_dir; winget takes care of path
        }
        else {
            $mingit_latest_url=(Invoke-WebRequest -Uri "https://raw.githubusercontent.com/git-for-windows/git-for-windows.github.io/refs/heads/main/latest-64-bit-mingit.url").Content
            Write-Host "Downloading mingit"
            Invoke-WebRequest -Uri "$mingit_latest_url" -Outfile "mingit_latest.zip"
            Expand-Archive -Path "mingit_latest.zip" -DestinationPath "$env:ProgramFiles/Git"
            $git_dir = "$env:ProgramFiles/Git"
        }

        Write-Host "git installed"
        Write-Host "You may want to configure git as well:"
        Write-Host '  git config --global user.email "you@somedomain.com"'
        Write-Host '  git config --global user.name "Your Name"'
        Write-Host '  git config --global core.editor "Your favorite editor"'
        Write-Host '  etc.'
    }

    if ($git_dir) {
      $env:Path="$git_dir\cmd;$git_dir\mingw64\bin;$git_dir\usr\bin;$env:Path"
    }
}


# Looking for vcpkg
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
    # Search the user's directory
    cd "~"
    $vcpkg_exe_path = Get-ChildItem -Path "vcpkg.exe" -Recurse `
                                        -ErrorAction SilentlyContinue| `
      Sort-Object -Descending -Property LastWriteTime | `
      Select -Index 0 | Select -ExpandProperty "FullName" | Split-Path
    if (($vcpkg_exe_path) -And (Test-Path -Path $vcpkg_exe_path)) {
        $env:VCPKG_DIR=$vcpkg_exe_path
    }
    else {
        # Search system drive, ugh
        cd "$env:SYSTEMDRIVE\"
        $vcpkg_exe_path = Get-ChildItem -Path "vcpkg.exe" -Recurse `
                                        -ErrorAction SilentlyContinue| `
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
            $vcpkg_exe_path = Get-ChildItem -Path "vcpkg.exe" -Recurse `
              -ErrorAction SilentlyContinue| `
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
    if (! (Test-Path -Path "$vcpkg_new_inst_dir")) {
        mkdir "$vcpkg_new_inst_dir"
        cd "$vcpkg_new_inst_dir"
        cd ..
    }
    git clone https://github.com/Microsoft/vcpkg.git
    cd vcpkg
    .\bootstrap-vcpkg.bat
    vcpkg.exe integrate install
    $env:VCPKG_DIR = $vcpkg_new_inst_dir
}

if (($env:VCPKG_DIR) -And (Test-Path -Path "$env:VCPKG_DIR")) {
    $env:VCPKG_INSTALLATION_ROOT=$env:VCPKG_DIR # As per github Windows images
    $env:VCPKG_ROOT=$env:VCPKG_INSTALLATION_ROOT
    $env:Path="$env:Path;$env:VCPKG_DIR"
}

if ((! (Get-Command pkg-config -errorAction SilentlyContinue)) -or `
  ((where.exe pkg-config) -like "*Strawberry*")) {
      # meson does not accept the version of pkg-config in Strawberry Perl

      if (Get-Command winget -errorAction SilentlyContinue)
      { # winget exists on newer versions of Windows 10 and on
        # Windows 11 and after. It is is not supported on Windows
        # Server 2019, and is experimental only on Windows Server
        # 2022.

          winget install bloodrock.pkg-config-lite # For pkg-config
      }
      else
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
          if (($my_vcpkg_pkgconfig_dir) -And `
            (Test-Path -Path $my_vcpkg_pkgconfig_dir)) {
                $my_vcpkg_pkgconfig_dir = `
                  "$env:VCPKG_DIR\installed\x64-windows\lib\pkgconfig"
                if ($env:PKG_CONFIG_PATH) {
                    $env:PKG_CONFIG_PATH = `
                      "$env:PKG_CONFIG_PATH;$my_vcpkg_pkgconfig_dir"
                }
                else {
                    $env:PKG_CONFIG_PATH="$my_vcpkg_pkgconfig_dir"
                }
            }
          else {
              throw "pkgconf not installed as expected"
          }
      }
  }

if (! (vcpkg list "brotli")) { vcpkg install brotli }
if (! (vcpkg list "zstd")) { vcpkg install zstd }

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
              New-Item -ItemType SymbolicLink -Path "$vcpkg_installed_pkgconf_dir\pkg-config.exe" -Target "$vcpkg_installed_pkgconf_dir\pkgconf.exe"
          }
      }
}
else {
    throw "$env:VCPKG_DIR\installed not found"
}

if ((! (Get-Command python3 -errorAction SilentlyContinue)) -and `
  (! (vcpkg list "python3"))) {
      if ((! (Get-Command python -errorAction SilentlyContinue)) -or `
        (! (((python --version).SubString(7, 1)/1) -ge 3))) {
            vcpkg install python3
        }
  }

if (! (Get-Command pip3 -errorAction SilentlyContinue)) {
    python -m ensurepip
}

function Find-Meson-Exe {
    if (Test-Path "~/AppData/Local/Packages") {
        cd "~/AppData/Local/Packages"
        $meson_exe_path = Get-ChildItem -Path "meson.exe" -Recurse | `
          Sort-Object -Descending -Property LastWriteTime | `
          Select -Index 0 | Select -ExpandProperty "FullName" | Split-Path
    }

    if (((! ($meson_exe_path)) -or (! (Test-Path -Path $meson_exe_path))) -and (Test-Path "~/AppData/Roaming/Python")) {
        cd "~/AppData/Roaming/Python"
        $meson_exe_path = Get-ChildItem -Path "meson.exe" -Recurse | `
          Sort-Object -Descending -Property LastWriteTime | `
          Select -Index 0 | Select -ExpandProperty "FullName" | Split-Path
    }

    if (((! ($meson_exe_path)) -or (! (Test-Path -Path $meson_exe_path))) -and (Test-Path "~/AppData")) {
        cd "~/AppData"
        $meson_exe_path = Get-ChildItem -Path "meson.exe" -Recurse | `
          Sort-Object -Descending -Property LastWriteTime | `
          Select -Index 0 | Select -ExpandProperty "FullName" | Split-Path
    }

    return $meson_exe_path
}

if (! (Get-Command meson -errorAction SilentlyContinue)) {
    $meson_exe_path = Find-Meson-Exe
    if ((! ($meson_exe_path)) -Or (! (Test-Path -Path $meson_exe_path))) {
        Write-Host "Installing meson with pip3"
        pip3 install --user meson
        $meson_exe_path = Find-Meson-Exe
    }

    if (($meson_exe_path) -And (Test-Path -Path $meson_exe_path)) {
        $env:Path="$env:Path;$meson_exe_path"
    }
    else {
        throw "ERROR: meson.exe directory not added to path"
    }
}

if (! (vcpkg list "curl[openssl]")) { # vcpkg list - list installed packages
    vcpkg install curl[openssl]
}

if (! (vcpkg list "openssl")) { vcpkg install openssl }
if (! (vcpkg list "libevent")) { vcpkg install libevent }

if ((! (Get-ChildItem -Path "$env:ProgramFiles\googletest*" `
  -ErrorAction SilentlyContinue)) -And `
  (! (Get-ChildItem -Path "${env:ProgramFiles(x86)}\googletest*" `
  -ErrorAction SilentlyContinue))) {
      git clone https://github.com/google/googletest.git
      cd googletest
      mkdir build
      cd build
      cmake ..
      cmake --build .
      cmake --install . --config Debug
      cd ~
  }

if (! (vcpkg list "date")) { vcpkg install date } # Howard-Hinnant-Date

if ((! (Get-ChildItem -Path "$env:ProgramFiles\zlib*" `
  -ErrorAction SilentlyContinue)) -And `
  (! (Get-ChildItem -Path "${env:ProgramFiles(x86)}\zlib*" `
  -ErrorAction SilentlyContinue))) {
      Invoke-WebRequest -Uri https://zlib.net/current/zlib.tar.gz -OutFile zlib.tar.gz
      tar -xvzf .\zlib.tar.gz
      cd "zlib*" # Adjusts to whatever we downloaded, e.g. zlib-1.3.1
      mkdir build
      cd build
      cmake ..
      cmake --build . --config Release
      cmake --install . --config Release
  }

if (! (Get-Command doxygen -errorAction SilentlyContinue)) {
    if (Test-Path -Path "$env:USERPROFILE\doxygen.bin") {
        $env:Path="$env:Path;$env:USERPROFILE\doxygen.bin"
    }
    else {
        if (Get-Command winget -errorAction SilentlyContinue)
        {
            winget install doxygen
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

# We want Visual Studio to be installed even if we're not going to use
# the Visual Studio compiler, so we can have access to the Windows
# SDK(s) that are sintalled along with Visual Studio
if (! ((Test-Path -Path "$env:ProgramFiles/Microsoft Visual Studio") -or `
  (Test-Path -Path "${env:ProgramFiles(x86)}/Microsoft Visual Studio"))) {
      Write-Host "No existing Visual Studio detected"
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
                  Write-Host "Fetching Visual Studio $vsyr vs_community.exe"
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

      vs_community.exe --includeRecommended --wait --norestart
  }


cd $savedpwd
