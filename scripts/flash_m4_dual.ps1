# Write CrossMux to BOTH ESP32-S3 OTA slots and force boot from app0.
# App-only flashes at 0x10000 leave otadata pointing at app1, so the device
# keeps running the previous image. This script closes that hole.
param(
    [string]$Port = "",
    [string]$Firmware = "",
    [int]$Baud = 921600
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Firmware) {
    $Firmware = Join-Path $root "firmware-m4-3.bin"
}
$bootApp0 = Join-Path $env:USERPROFILE ".platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin"
$esptool = Join-Path $env:USERPROFILE ".platformio\packages\tool-esptoolpy\esptool.py"

if (-not (Test-Path $Firmware)) { throw "Firmware not found: $Firmware" }
if (-not (Test-Path $bootApp0)) { throw "boot_app0.bin not found: $bootApp0" }
if (-not (Test-Path $esptool)) { throw "esptool.py not found: $esptool" }

$size = (Get-Item $Firmware).Length
if ($size -gt 0x640000) { throw "Firmware $size bytes is larger than the 0x640000 app slot" }

function Get-UsbSerialPorts {
    $names = [System.IO.Ports.SerialPort]::GetPortNames()
    $usb = @()
    foreach ($name in $names) {
        $pnp = Get-PnpDevice -Class Ports -ErrorAction SilentlyContinue |
            Where-Object { $_.FriendlyName -match [regex]::Escape($name) }
        $isBluetooth = $false
        foreach ($dev in $pnp) {
            if ($dev.InstanceId -match 'BTHENUM') { $isBluetooth = $true }
        }
        if (-not $isBluetooth) { $usb += $name }
    }
    return $usb
}

if (-not $Port) {
    $usbPorts = @(Get-UsbSerialPorts)
    if ($usbPorts.Count -eq 1) {
        $Port = $usbPorts[0]
    } elseif ($usbPorts.Count -gt 1) {
        throw ("Multiple USB serial ports: {0}. Pass -Port COMx" -f ($usbPorts -join ", "))
    } else {
        throw "No USB serial port found. Plug the M4 in over USB (not Bluetooth COM5/COM6) and pass -Port COMx."
    }
}

Write-Host "Port     : $Port"
Write-Host "Firmware : $Firmware ($size bytes)"
Write-Host "Writing  : otadata@0xE000  app0@0x10000  app1@0x650000"
Write-Host "This overwrites the inactive-slot SD font cache. It will rebuild from the SD card on first boot."

python $esptool --chip esp32s3 --port $Port --baud $Baud write-flash `
    --flash-mode dio --flash-freq 80m --flash-size 16MB `
    0xE000 $bootApp0 `
    0x10000 $Firmware `
    0x650000 $Firmware

if ($LASTEXITCODE -ne 0) { throw "esptool failed with exit $LASTEXITCODE" }
Write-Host "Flash OK. Unplug, power on, then check 设置 -> 关于 -> 固件版本 == 1.5.7-m4-3"
