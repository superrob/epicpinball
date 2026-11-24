<#
  Build epicpinball-dof.exe, the program that drives a cabinet's solenoids
  through libdof, into dof\build\. GPLv3 like libdof (dof\LICENSE); the game
  never links it, and nothing builds it unless this is run. Windows' twin of
  dof/build.sh.

    powershell -ExecutionPolicy Bypass -File dof\build.ps1          # fetch what is missing, then build
    powershell -ExecutionPolicy Bypass -File dof\build.ps1 -Fetch   # only fetch: the rest then needs no network

  Fetched, each at the commit build.sh pins and checked by the same SHA-256:
  libdof and the four libraries its own script pins. libdof gets dof\patches\
  (git apply). All of it is built with Visual Studio 2022, x64: libdof with its
  own PLATFORM=win ARCH=x64, as VPinball's Windows builds use it. libdof's own
  script builds libusb, libserialport, hidapi and libftdi with MSYS2's MinGW;
  this needs no MSYS2: libusb and libserialport build from their own Visual
  Studio projects, hidapi and libftdi with CMake (libftdi with
  dof\win\msvc-gettimeofday.h, and every function exported, as it has no
  export list of its own). Their import libraries go under the names libdof's
  CMake links (libusb64-1.0, libserialport64, hidapi64, libftdi164); the DLLs
  keep their own names and go beside epicpinball-dof.exe in dof\build.
  Nothing is installed outside dof\build.

  Needs Visual Studio 2022 with C++ (the Build Tools are enough:
  scripts\setup.ps1 installs them), CMake 3.25, git (for git apply);
  curl.exe and tar.exe come with Windows 10 and 11.
#>
param([switch]$Fetch)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$Here = (Get-Location).Path
$B = Join-Path $Here "build"
$S = Join-Path $B "src"
$L = Join-Path $B "lib"

function Fail($m) { Write-Host "dof/build.ps1: $m" -ForegroundColor Red; exit 1 }
# A native command; its exit code, not its stderr, says whether it worked
function Native([scriptblock]$cmd) {
    $ErrorActionPreference = "Continue"
    & $cmd | Out-Host
    if ($LASTEXITCODE -ne 0) { Fail "failed ($LASTEXITCODE): $cmd" }
}
function Have($exe) { return [bool](Get-Command $exe -ErrorAction SilentlyContinue) }

foreach ($t in "cmake", "git", "curl.exe", "tar.exe") {
    if (-not (Have $t)) { Fail "$t is missing" }
}
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = $null
if (Test-Path $vswhere) {
    $msbuild = & $vswhere -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
}
if (-not $msbuild) { Fail "Visual Studio 2022 with C++ is missing (scripts\setup.ps1 installs the Build Tools)" }
New-Item -ItemType Directory -Force (Join-Path $S "dl"), $L | Out-Null

# name owner/repo commit sha256 [a path in the archive to leave out]
function Fetch-Pinned($name, $repo, $commit, $sum, $skip) {
    $tgz = Join-Path $S "dl\$name.tar.gz"
    if (-not (Test-Path $tgz)) {
        Write-Host "fetching $repo $commit"
        Native { curl.exe -fsSL --max-time 180 -o "$tgz.part" "https://codeload.github.com/$repo/tar.gz/$commit" }
        Move-Item -Force "$tgz.part" $tgz
    }
    if ((Get-FileHash -Algorithm SHA256 $tgz).Hash.ToLower() -ne $sum) {
        Fail "$tgz is not the pinned archive: delete it and run again"
    }
    $dir = Join-Path $S $name
    if (-not (Test-Path $dir)) {
        $t = Join-Path $S (".$name." + [guid]::NewGuid().ToString("N").Substring(0, 6))
        New-Item -ItemType Directory $t | Out-Null
        $x = @()
        if ($skip) { $x = @("--exclude", "*/$skip") }
        Native { tar.exe -xzf $tgz -C $t --strip-components=1 @x }
        Move-Item $t $dir
    }
}
Fetch-Pinned libdof vpinball/libdof 3d0ad20a4be185aaa3d426e1abff01f9639a2f62 `
    5fb82f9b46a6e55768dc1254fb9d9e9c2d30374486ce1686317eaa4408b1fb40
# its README.md is a symbolic link, which tar cannot make without the right to
Fetch-Pinned libusb libusb/libusb 15a7ebb4d426c5ce196684347d2b7cafad862626 `
    4ae78a9c6c540a3ccd7153c77e24947fa2ba50d845714bbbfccf8783832ed704 README.md
Fetch-Pinned libserialport sigrokproject/libserialport 21b3dfe5f68c205be4086469335fd2fc2ce11ed2 `
    66c5eb23811b58938dc97168ec9acb0568dcebb02e3d97dcec30ae22a524eaf6
Fetch-Pinned hidapi libusb/hidapi d6b2a974608dec3b76fb1e36c189f22b9cf3650c `
    a93a9bfb599831b82c6cef50f42359e0f5f31abfc8f6954862c328a464cbc4d0
Fetch-Pinned libftdi jsm174/libftdi 8b138364e5577ec6bf3778283347eaddb83f0429 `
    fbd57dce21ce8408dd00593befc075bf84c64b7c1972a1583b83a09ad5dd5637
if ($Fetch) { exit 0 }

# A file the build made, found wherever the generator put it
function Built($dir, $name) {
    $f = Get-ChildItem -Path $dir -Recurse -File -Filter $name -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $f) { Fail "no $name under $dir after building it" }
    return $f.FullName
}
# Configure and build with CMake, Visual Studio 2022, x64, Release. The
# build folders are build-win: a tree Linux built too keeps its own build.
function CMake-Build($src, $target, [string[]]$defs) {
    $bld = Join-Path $src "build-win"
    Native { cmake -S $src -B $bld -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release @defs }
    $t = @()
    if ($target) { $t = @("--target", $target) }
    Native { cmake --build $bld --config Release --parallel @t }
}

$T = Join-Path $S "libdof\third-party"
$I = Join-Path $T "include"
$BL = Join-Path $T "build-libs\win\x64"
$R = Join-Path $T "runtime-libs\win\x64"
New-Item -ItemType Directory -Force (Join-Path $I "libusb-1.0"), (Join-Path $I "libftdi1"), $BL, $R | Out-Null

# What libdof's platforms/win/x64/external.sh does, minus its downloads, with Visual Studio
$usb = Join-Path $S "libusb"
if (-not (Test-Path (Join-Path $S ".libusb-built-win"))) {
    # its project makes every warning an error, and newer compilers than its
    # own warn (C5287): cl.exe adds what _CL_ holds after the project's options
    $env:_CL_ = "/WX-"
    Native { & $msbuild (Join-Path $usb "msvc\libusb_dll.vcxproj") /m /nologo /p:Configuration=Release /p:Platform=x64 }
    Remove-Item Env:_CL_
    New-Item -ItemType File (Join-Path $S ".libusb-built-win") | Out-Null
}
$usbdll = Built (Join-Path $usb "build") "libusb-1.0.dll"
$usblib = [IO.Path]::ChangeExtension($usbdll, ".lib")
Copy-Item (Join-Path $usb "libusb\libusb.h") (Join-Path $I "libusb-1.0")
Copy-Item $usblib (Join-Path $BL "libusb64-1.0.lib")
Copy-Item $usbdll $R

$sp = Join-Path $S "libserialport"
if (-not (Test-Path (Join-Path $S ".libserialport-built-win"))) {
    Native { & $msbuild (Join-Path $sp "libserialport.vcxproj") /m /nologo /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 }
    New-Item -ItemType File (Join-Path $S ".libserialport-built-win") | Out-Null
}
$spdll = Built $sp "libserialport.dll"
Copy-Item (Join-Path $sp "libserialport.h") $I
Copy-Item ([IO.Path]::ChangeExtension($spdll, ".lib")) (Join-Path $BL "libserialport64.lib")
Copy-Item $spdll $R

$hid = Join-Path $S "hidapi"
if (-not (Test-Path (Join-Path $S ".hidapi-built-win"))) {
    CMake-Build $hid "" @("-DBUILD_SHARED_LIBS=ON")
    New-Item -ItemType File (Join-Path $S ".hidapi-built-win") | Out-Null
}
$hiddll = Built (Join-Path $hid "build-win") "hidapi.dll"
Copy-Item -Recurse -Force (Join-Path $hid "hidapi") $I
Copy-Item ([IO.Path]::ChangeExtension($hiddll, ".lib")) (Join-Path $BL "hidapi64.lib")
Copy-Item $hiddll $R

$ftdi = Join-Path $S "libftdi"
if (-not (Test-Path (Join-Path $S ".libftdi-built-win"))) {
    CMake-Build $ftdi "" @("-DCMAKE_POLICY_VERSION_MINIMUM=3.10", "-DFTDI_EEPROM=OFF", "-DEXAMPLES=OFF", "-DSTATICLIBS=OFF",
        "-DLIBUSB_INCLUDE_DIR=$(Join-Path $usb 'libusb')", "-DLIBUSB_LIBRARIES=$usblib",
        "-DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=ON",
        "-DCMAKE_PROJECT_INCLUDE=$(Join-Path $Here 'win\libftdi-msvc.cmake')",
        "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /D_FTDI_DISABLE_DEPRECATED")
    New-Item -ItemType File (Join-Path $S ".libftdi-built-win") | Out-Null
}
$ftdidll = Built (Join-Path $ftdi "build-win") "ftdi1.dll"
Copy-Item (Join-Path $ftdi "src\ftdi.h") (Join-Path $I "libftdi1")
Copy-Item ([IO.Path]::ChangeExtension($ftdidll, ".lib")) (Join-Path $BL "libftdi164.lib")
Copy-Item $ftdidll $R

# Named by what is in the patch, so a tree built without it is built again.
# Applied with LF line ends, as libdof's files have: git for Windows checks
# this repository out with CRLF when core.autocrlf is on (GitHub Desktop's).
$Patch = Join-Path $S "pico-output-bound.patch"
[IO.File]::WriteAllText($Patch, ([IO.File]::ReadAllText((Join-Path $Here "patches\pico-output-bound.patch")) -replace "`r`n", "`n"))
$Sum = (Get-FileHash -Algorithm SHA256 $Patch).Hash.ToLower().Substring(0, 16)
if (-not (Test-Path (Join-Path $S ".libdof-patched-$Sum"))) {
    # outside any repository (this one holds dof\build): git apply works as patch does
    $env:GIT_CEILING_DIRECTORIES = $S
    Push-Location (Join-Path $S "libdof")
    $ErrorActionPreference = "Continue"
    & git apply -p1 $Patch
    $rc = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    Pop-Location
    if ($rc -ne 0) {
        Fail "dof\patches\pico-output-bound.patch does not apply to $S\libdof: delete that folder and $S\.libdof-*, and run again"
    }
    New-Item -ItemType File (Join-Path $S ".libdof-patched-$Sum") | Out-Null
}
$dof = Join-Path $S "libdof"
if (-not (Test-Path (Join-Path $S ".libdof-built-win-$Sum"))) {
    CMake-Build $dof "dof_shared" @("-DPLATFORM=win", "-DARCH=x64", "-DBUILD_STATIC=OFF", "-DPOST_BUILD_COPY_EXT_LIBS=OFF")
    New-Item -ItemType File (Join-Path $S ".libdof-built-win-$Sum") | Out-Null
}
$dofdll = Built (Join-Path $dof "build-win") "dof64.dll"
Copy-Item ([IO.Path]::ChangeExtension($dofdll, ".lib")) $L
Copy-Item $dofdll, (Join-Path $R "*.dll") $B

$hb = Join-Path $B "helper-win"
Native { cmake -S $Here -B $hb -G "Visual Studio 17 2022" -A x64 }
Native { cmake --build $hb --config Release --parallel }
Copy-Item (Built $hb "epicpinball-dof.exe") (Join-Path $B "epicpinball-dof.exe.new")
Move-Item -Force (Join-Path $B "epicpinball-dof.exe.new") (Join-Path $B "epicpinball-dof.exe")
Write-Host "built $B\epicpinball-dof.exe"
