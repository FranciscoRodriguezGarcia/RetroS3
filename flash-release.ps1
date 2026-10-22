param(
    [string]$Port = "COM7"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# Keep the existing board-specific sdkconfig. Defaults are only applied to
# options not already set in sdkconfig, so verify the effective configuration.
& idf.py reconfigure
if ($LASTEXITCODE -ne 0) { throw "ESP-IDF configuration failed" }

if (-not (Select-String -Path "sdkconfig" -Pattern '^CONFIG_COMPILER_OPTIMIZATION_PERF=y$' -Quiet)) {
    throw "Refusing to flash Debug firmware. Run 'idf.py menuconfig' and select Compiler options > Optimize for performance (-O2), then retry."
}

if (-not (Select-String -Path "sdkconfig" -Pattern '^CONFIG_IDF_TARGET="esp32s3"$' -Quiet)) {
    throw "Refusing to flash: the configured target is not ESP32-S3."
}

& idf.py build
if ($LASTEXITCODE -ne 0) { throw "ESP-IDF build failed; nothing was flashed" }

& idf.py -p $Port flash monitor
if ($LASTEXITCODE -ne 0) { throw "Flash/monitor failed" }
