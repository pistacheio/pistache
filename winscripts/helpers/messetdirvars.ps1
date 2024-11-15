# PowerShell Script

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#
# Sets MESON_BUILD_DIR and MESON_PREFIX_DIR
#
# Use by:
#   . $PSScriptRoot/helpers/messetdirvars.ps1

# To see available properties:
#   (Get-CIMClass -classname "CIM_Processor").CimClassProperties | ft -wrap -autosize
#   (Get-CIMClass -classname CIM_OperatingSystem).CimClassProperties | ft -wrap -autosize
#
# (Get-CimInstance CIM_Processor).AddressWidth is a UInt16, e.g.: 64
#
# (Get-CimInstance CIM_Processor).Caption is CimType String
#   e.g.: Intel64 Family 6 Model 158 Stepping 9

$MY_ARCH_NM="x86"
$cpu_cim_width=(Get-CimInstance CIM_Processor).AddressWidth
$cpu_cim_caption=(Get-CimInstance CIM_Processor).Caption
$cpu_cim_caption_lwr=$cpu_cim_caption.ToLower()
if ((-not ($cpu_cim_caption_lwr.contains("intel"))) -and `
    (-not ($cpu_cim_caption.contains("AMD"))) -and `
    (-not ($cpu_cim_caption_lwr.contains("advanced micro devices"))) -and `
    (($cpu_cim_caption_lwr.contains("arm")))) {
  # Assume this is an ARM of some kind
  if ($cpu_cim_width -eq 32) {$MY_ARCH_NM="a32"}
  else {$MY_ARCH_NM="a64"}
}
else {
  # Assume this is an Intel or AMD processor of some kind
  if ($cpu_cim_width -eq 32) {$MY_ARCH_NM="x32"}
  else {$MY_ARCH_NM="x86"}
}

# On 32-bit Windows, there is only the "C:\Program Files" program
# folder, no "C:\Program Files (x86)"
# On 64-bit Windows, 64-bit programs go in "C:\Program Files", while
# 32-bit programs go in "C:\Program Files (x86)".
#
# So we will always want to use "C:\Program Files" as the destination
# for Pistache unless we were compiling a 32-bit Pistache library on
# 64-bit windows.
#
# Using the "pistache_distribution" directory name is modelled on the
# behaviour of googletest upon install.

if ([Environment]::Is64BitOperatingSystem) {
    $MESON_BUILD_DIR="build$MY_ARCH_NM.mes.w64"
    $MESON_PREFIX_DIR="$env:ProgramFiles\pistache_distribution"
}
else {
    $MESON_BUILD_DIR="build$MY_ARCH_NM.mes.w32"
    $MESON_PREFIX_DIR="$env:ProgramFiles\pistache_distribution"
}
