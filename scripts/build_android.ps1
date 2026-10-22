param(
    [string]$Assets = "$env:USERPROFILE/Documents/SWOS/assets",
    [string]$Sdk = $env:ANDROID_HOME
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (!$Sdk) { $Sdk = Join-Path $repo 'build/android-sdk' }
if (!(Test-Path "$Sdk/ndk/28.2.13676358")) {
    throw 'Install Android SDK platform 35, build-tools 34.0.0 and NDK 28.2.13676358; pass -Sdk or set ANDROID_HOME.'
}
$sdl = Join-Path $repo 'build/android-deps/SDL2-2.32.10'
if (!(Test-Path $sdl)) {
    New-Item -ItemType Directory -Force "$repo/build/android-deps" | Out-Null
    Invoke-WebRequest 'https://www.libsdl.org/release/SDL2-2.32.10.zip' -OutFile "$repo/build/android-deps/sdl.zip"
    Expand-Archive "$repo/build/android-deps/sdl.zip" "$repo/build/android-deps" -Force
}
Push-Location $repo
try {
    python scripts/stage_android.py --assets $Assets
    if ($LASTEXITCODE) { throw 'Runtime staging failed' }
    $sdkPath = (Resolve-Path $Sdk).Path.Replace('\', '/')
    Set-Content android/local.properties "sdk.dir=$sdkPath" -Encoding ascii
    & android/gradlew.bat -p android assembleDebug
    if ($LASTEXITCODE) { throw 'Android build failed' }
    New-Item -ItemType Directory -Force dist/android | Out-Null
    Copy-Item android/app/build/outputs/apk/debug/app-debug.apk dist/android/OpenSWOS-test-arm64.apk
    Write-Host "APK: $repo/dist/android/OpenSWOS-test-arm64.apk"
} finally { Pop-Location }
