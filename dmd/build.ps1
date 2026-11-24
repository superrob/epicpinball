<#
  Build epicpinball-dmd.exe, the program that shows the game's DMD picture on
  a real DMD (ZeDMD, Pixelcade, PinDMD3) through libdmdutil, into dmd\build\.
  GPLv3 like libdmdutil (dmd\LICENSE); the game never links it, and nothing
  builds it unless this is run. Windows' twin of dmd/build.sh.

    powershell -ExecutionPolicy Bypass -File dmd\build.ps1          # fetch what is missing, then build
    powershell -ExecutionPolicy Bypass -File dmd\build.ps1 -Fetch   # only fetch: the rest then needs no network

  Fetched, each at the commit build.sh pins and checked by the same SHA-256:
  libdmdutil, the libzedmd its own script pins, and the four libraries
  libzedmd's pins. Serum, VNI, PUP and PIN2DMD are left out (colourisation and
  libusb): the game's picture is already in colour. All of it is built with
  Visual Studio 2022, x64: libzedmd and libdmdutil with their own
  PLATFORM=win ARCH=x64, cargs and sockpp with CMake as their Windows scripts
  do, and libserialport from its own Visual Studio project (their scripts use
  MSYS2's MinGW for that one; this needs no MSYS2). Import libraries go under
  the names their CMake links (cargs64, sockpp64, libserialport64); the DLLs
  keep their own names and go beside epicpinball-dmd.exe in dmd\build.
  Nothing is installed outside dmd\build.

  Needs Visual Studio 2022 with C++ (the Build Tools are enough:
  scripts\setup.ps1 installs them), CMake 3.25; curl.exe and tar.exe come with
  Windows 10 and 11.
#>
param([switch]$Fetch)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$Here = (Get-Location).Path
$B = Join-Path $Here "build"
$S = Join-Path $B "src"
$L = Join-Path $B "lib"

function Fail($m) { Write-Host "dmd/build.ps1: $m" -ForegroundColor Red; exit 1 }
# A native command; its exit code, not its stderr, says whether it worked
function Native([scriptblock]$cmd) {
    $ErrorActionPreference = "Continue"
    & $cmd | Out-Host
    if ($LASTEXITCODE -ne 0) { Fail "failed ($LASTEXITCODE): $cmd" }
}
function Have($exe) { return [bool](Get-Command $exe -ErrorAction SilentlyContinue) }

foreach ($t in "cmake", "curl.exe", "tar.exe") {
    if (-not (Have $t)) { Fail "$t is missing" }
}
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = $null
if (Test-Path $vswhere) {
    $msbuild = & $vswhere -latest -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
}
if (-not $msbuild) { Fail "Visual Studio 2022 with C++ is missing (scripts\setup.ps1 installs the Build Tools)" }
New-Item -ItemType Directory -Force (Join-Path $S "dl"), $L | Out-Null

# name owner/repo commit sha256
function Fetch-Pinned($name, $repo, $commit, $sum) {
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
        Native { tar.exe -xzf $tgz -C $t --strip-components=1 }
        Move-Item $t $dir
    }
}
Fetch-Pinned libdmdutil vpinball/libdmdutil 6b8f711a5346cf2e70562872ebc1a57b210906df `
    9c3589e173a8c882f6fcb22ceba6accc05762afc22652f971e9b7750f7b0283a
Fetch-Pinned libzedmd PPUC/libzedmd ed69d499d87d4d328bd328773cc6bbd3dadd64c3 `
    9a278ce2bd08b0fab79d791c815ce5f2cade04bf18bccf6689a02c5a874137bb
Fetch-Pinned cargs likle/cargs 0698c3f90333446d0fc2745c1e9ce10dd4a9497a `
    c948f6d3a4f347ed6440d474a82d1a5ce7201c5e6c87df1f3fd9399581a0914a
Fetch-Pinned libserialport sigrokproject/libserialport 21b3dfe5f68c205be4086469335fd2fc2ce11ed2 `
    66c5eb23811b58938dc97168ec9acb0568dcebb02e3d97dcec30ae22a524eaf6
Fetch-Pinned libframeutil ppuc/libframeutil 711ab21dcf9ad33d8db8339dfd3b1548bcc34cdb `
    3dd0809ace8353fbd1f1b070f4665461ed5acae11578808a0112a0676c811dfe
Fetch-Pinned sockpp fpagliughi/sockpp e6c4688a576d95f42dd7628cefe68092f6c5cd0f `
    fabb73eb4a86a3314b0639740e1d77073775d8c98960654b0e9f2e4474695491
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
$Off = @("-DENABLE_SERUM=OFF", "-DENABLE_VNI=OFF", "-DENABLE_PUP=OFF", "-DENABLE_PIN2DMD=OFF")

# What libzedmd's platforms/win/x64/external.sh does, minus its downloads, with Visual Studio
$Z = Join-Path $S "libzedmd\third-party"
$ZI = Join-Path $Z "include"
$ZB = Join-Path $Z "build-libs\win\x64"
$ZR = Join-Path $Z "runtime-libs\win\x64"
New-Item -ItemType Directory -Force $ZI, $ZB, $ZR | Out-Null
$cargs = Join-Path $S "cargs"
if (-not (Test-Path (Join-Path $S ".cargs-built-win"))) {
    CMake-Build $cargs "" @("-DBUILD_SHARED_LIBS=ON")
    New-Item -ItemType File (Join-Path $S ".cargs-built-win") | Out-Null
}
$dll = Built (Join-Path $cargs "build-win") "cargs.dll"
Copy-Item (Join-Path $cargs "include\cargs.h") $ZI
Copy-Item ([IO.Path]::ChangeExtension($dll, ".lib")) (Join-Path $ZB "cargs64.lib")
Copy-Item $dll $ZR

$sp = Join-Path $S "libserialport"
if (-not (Test-Path (Join-Path $S ".libserialport-built-win"))) {
    Native { & $msbuild (Join-Path $sp "libserialport.vcxproj") /m /nologo /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 }
    New-Item -ItemType File (Join-Path $S ".libserialport-built-win") | Out-Null
}
$dll = Built $sp "libserialport.dll"
Copy-Item (Join-Path $sp "libserialport.h") $ZI
Copy-Item ([IO.Path]::ChangeExtension($dll, ".lib")) (Join-Path $ZB "libserialport64.lib")
Copy-Item $dll $ZR

Copy-Item -Recurse -Force (Join-Path $S "libframeutil\include\*") $ZI

$sock = Join-Path $S "sockpp"
if (-not (Test-Path (Join-Path $S ".sockpp-built-win"))) {
    CMake-Build $sock "" @()
    New-Item -ItemType File (Join-Path $S ".sockpp-built-win") | Out-Null
}
$dll = Built (Join-Path $sock "build-win") "sockpp.dll"
Copy-Item -Recurse -Force (Join-Path $sock "include\sockpp") $ZI
Copy-Item ([IO.Path]::ChangeExtension($dll, ".lib")) (Join-Path $ZB "sockpp64.lib")
Copy-Item $dll $ZR

$zedmd = Join-Path $S "libzedmd"
if (-not (Test-Path (Join-Path $S ".libzedmd-built-win"))) {
    CMake-Build $zedmd "zedmd_shared" @("-DPLATFORM=win", "-DARCH=x64", "-DBUILD_SHARED=ON", "-DBUILD_STATIC=OFF", "-DPOST_BUILD_COPY_EXT_LIBS=OFF")
    New-Item -ItemType File (Join-Path $S ".libzedmd-built-win") | Out-Null
}
$zeddll = Built (Join-Path $zedmd "build-win") "zedmd64.dll"

# What libdmdutil's platforms/win/x64/external.sh does with libzedmd, minus its downloads
$T = Join-Path $S "libdmdutil\third-party"
$TI = Join-Path $T "include"
$TB = Join-Path $T "build-libs\win\x64"
$TR = Join-Path $T "runtime-libs\win\x64"
New-Item -ItemType Directory -Force $TI, $TB, $TR | Out-Null
Copy-Item (Join-Path $zedmd "src\ZeDMD.h"), (Join-Path $ZI "libserialport.h"), (Join-Path $ZI "cargs.h"), (Join-Path $ZI "FrameUtil.h") $TI
Copy-Item -Recurse -Force (Join-Path $ZI "komihash"), (Join-Path $ZI "sockpp") $TI
Copy-Item (Join-Path $ZB "*.lib") $TB
Copy-Item (Join-Path $ZR "*.dll") $TR
Copy-Item ([IO.Path]::ChangeExtension($zeddll, ".lib")) $TB
Copy-Item $zeddll $TR
$dmdutil = Join-Path $S "libdmdutil"
if (-not (Test-Path (Join-Path $S ".libdmdutil-built-win"))) {
    CMake-Build $dmdutil "dmdutil_shared" (@("-DPLATFORM=win", "-DARCH=x64", "-DBUILD_SHARED=ON", "-DBUILD_STATIC=OFF", "-DPOST_BUILD_COPY_EXT_LIBS=OFF") + $Off)
    New-Item -ItemType File (Join-Path $S ".libdmdutil-built-win") | Out-Null
}
$utildll = Built (Join-Path $dmdutil "build-win") "dmdutil64.dll"
Copy-Item ([IO.Path]::ChangeExtension($utildll, ".lib")) $L
Copy-Item $utildll, (Join-Path $TR "*.dll") $B

$hb = Join-Path $B "helper-win"
Native { cmake -S $Here -B $hb -G "Visual Studio 17 2022" -A x64 }
Native { cmake --build $hb --config Release --parallel }
Copy-Item (Built $hb "epicpinball-dmd.exe") (Join-Path $B "epicpinball-dmd.exe.new")
Move-Item -Force (Join-Path $B "epicpinball-dmd.exe.new") (Join-Path $B "epicpinball-dmd.exe")
Write-Host "built $B\epicpinball-dmd.exe"
