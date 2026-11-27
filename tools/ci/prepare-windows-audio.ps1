# Only for disposable native Windows Actions test runners; never used in product installers.
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('x86_64', 'arm64')]
    [string] $Architecture,
    [Parameter(Mandatory)]
    [string] $EvidenceDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
$evidence = [ordered]@{
    sourceSha = (& git rev-parse HEAD)
    architecture = $Architecture
    packageUrl = 'https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack46.zip'
    expectedSha256 = 'bc44e8974191a5a2427d6c979e2dab08dbb09925f3f435dfa2f0ba3e45b6eb90'
    signatures = @()
    result = 'failed'
}

function Assert-TrustedSignature([string] $Path, [string] $Publisher) {
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    $name = if ($signature.SignerCertificate) {
        $signature.SignerCertificate.GetNameInfo([System.Security.Cryptography.X509Certificates.X509NameType]::SimpleName, $false)
    } else { '' }
    $evidence.signatures += [ordered]@{
        file = [IO.Path]::GetFileName($Path)
        status = [string]$signature.Status
        statusMessage = $signature.StatusMessage
        publisher = $name
        subject = if ($signature.SignerCertificate) { $signature.SignerCertificate.Subject } else { $null }
        thumbprint = if ($signature.SignerCertificate) { $signature.SignerCertificate.Thumbprint } else { $null }
        timestampSubject = if ($signature.TimeStamperCertificate) { $signature.TimeStamperCertificate.Subject } else { $null }
    }
    if ($signature.Status -ne 'Valid' -or $name -cne $Publisher) {
        throw "Untrusted signature or unexpected publisher for ${Path}: $($signature.Status), $name"
    }
}

try {
    if (!$IsWindows -or $env:GITHUB_ACTIONS -ne 'true' -or !$env:GITHUB_ENV) {
        throw 'This script requires a disposable Windows GitHub Actions runner'
    }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Driver installation requires an elevated runner'
    }
    $nativeArch = if ($Architecture -eq 'arm64') { 'Arm64' } else { 'X64' }
    $evidence.osArchitecture = [string][Runtime.InteropServices.RuntimeInformation]::OSArchitecture
    $evidence.processArchitecture = [string][Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture
    $evidence.os = [Runtime.InteropServices.RuntimeInformation]::OSDescription
    if ($evidence.osArchitecture -ne $nativeArch -or $evidence.processArchitecture -ne $nativeArch) {
        throw "Native $nativeArch OS and PowerShell are required"
    }

    $work = Join-Path $env:RUNNER_TEMP 'cirrus-vbcable-pack46'
    New-Item -ItemType Directory -Path $work | Out-Null
    $archive = Join-Path $work 'VBCABLE_Driver_Pack46.zip'
    Invoke-WebRequest -Uri $evidence.packageUrl -OutFile $archive
    $evidence.actualSha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($evidence.actualSha256 -cne $evidence.expectedSha256) { throw 'Pack46 SHA-256 mismatch' }
    $package = Join-Path $work 'driver'
    Expand-Archive -LiteralPath $archive -DestinationPath $package
    $installer = Join-Path $package 'VBCABLE_Setup_x64.exe'
    Assert-TrustedSignature $installer 'BUREL VINCENT Entrepreneur individuel'
    Assert-TrustedSignature (Join-Path $package 'vbaudio_cable64_win10.cat') 'Microsoft Windows Hardware Compatibility Publisher'

    # The fixed package includes native AMD64 and ARM64 drivers. Its official setup
    # executable is AMD64 on both OSes; on Win11 ARM64 only setup uses OS emulation.
    # Do not stage an INF as a substitute for creating the actual PnP device. Normal
    # Windows PnP catalog/member validation remains enforced; no trust-policy changes.
    $evidence.installerArguments = @('-i', '-h')
    $process = Start-Process -FilePath $installer -ArgumentList $evidence.installerArguments `
        -WorkingDirectory $package -PassThru `
        -RedirectStandardOutput (Join-Path $EvidenceDirectory 'installer-stdout.txt') `
        -RedirectStandardError (Join-Path $EvidenceDirectory 'installer-stderr.txt')
    if (!$process.WaitForExit(120000)) {
        Stop-Process -Id $process.Id -Force
        throw 'Official silent setup timed out (unsupported switches or interactive/reboot prompt)'
    }
    $process.WaitForExit()
    $evidence.installerExitCode = $process.ExitCode
    if ($process.ExitCode -in @(3010, 1641)) { throw 'Official setup requires a runner reboot' }
    if ($process.ExitCode -ne 0) { throw "Official setup failed: exit $($process.ExitCode)" }

    # Refresh service enumeration once after installation, without rebooting the OS.
    if ((Get-Service Audiosrv).Status -ne 'Stopped') { Stop-Service Audiosrv }
    if ((Get-Service AudioEndpointBuilder).Status -ne 'Stopped') { Stop-Service AudioEndpointBuilder }
    Start-Service AudioEndpointBuilder
    Start-Service Audiosrv
    foreach ($service in @('AudioEndpointBuilder', 'Audiosrv')) {
        (Get-Service $service).WaitForStatus('Running', [TimeSpan]::FromSeconds(30))
    }
    $evidence.services = @(Get-Service AudioEndpointBuilder, Audiosrv | Select-Object Name, @{n='Status'; e={[string]$_.Status}})

    # IMMDeviceEnumerator reads the default render device for eMultimedia, the role
    # used by QMediaDevices on Windows. HRESULTs are checked by COM interop.
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace CirrusCI {
    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
    class MMDeviceEnumerator { }
    [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceEnumerator {
        void EnumAudioEndpoints(int flow, uint mask, out IntPtr devices);
        void GetDefaultAudioEndpoint(int flow, int role, out IMMDevice device);
    }
    [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDevice {
        void Activate(ref Guid iid, uint context, IntPtr parameters, out IntPtr instance);
        void OpenPropertyStore(uint access, out IntPtr properties);
        void GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
        void GetState(out uint state);
    }
    public static class DefaultRender {
        public static string ActiveMultimediaId() {
            var enumerator = (IMMDeviceEnumerator)new MMDeviceEnumerator();
            IMMDevice device = null;
            try {
                try { enumerator.GetDefaultAudioEndpoint(0, 1, out device); }
                catch (COMException e) when (e.HResult == unchecked((int)0x80070490)) { return null; }
                uint state;
                device.GetState(out state);
                if (state != 1) return null; // DEVICE_STATE_ACTIVE
                string id;
                device.GetId(out id);
                return id;
            } finally {
                if (device != null) Marshal.ReleaseComObject(device);
                Marshal.ReleaseComObject(enumerator);
            }
        }
    }
}
'@

    # Only readiness is polled: never reinstall, accept a different device or retry
    # a signature/network/installer failure. A different default is an explicit failure.
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    $ready = $false
    do {
        $audioEndpoints = @(Get-PnpDevice -PresentOnly | Where-Object { $_.Class -eq 'AudioEndpoint' })
        $endpoints = @($audioEndpoints | Where-Object {
            $_.InstanceId -like 'SWD\MMDEVAPI\{0.0.0.00000000}.*' -and
            $_.FriendlyName -match '^CABLE Input \(VB-Audio Virtual Cable\)$' -and $_.Status -eq 'OK'
        })
        $defaultId = [CirrusCI.DefaultRender]::ActiveMultimediaId()
        if ($endpoints.Count -eq 1 -and $defaultId) {
            $endpointId = $endpoints[0].InstanceId.Substring('SWD\MMDEVAPI\'.Length)
            $ready = $endpointId -ieq $defaultId
        }
        if (!$ready) { Start-Sleep -Seconds 2 }
    } while (!$ready -and [DateTime]::UtcNow -lt $deadline)
    $evidence.audioEndpoints = @($audioEndpoints | Select-Object InstanceId, FriendlyName, Status)
    $evidence.renderEndpoints = @($endpoints | Select-Object InstanceId, FriendlyName, Status)
    $evidence.defaultMultimediaRenderId = $defaultId
    if (!$ready) { throw 'VB-CABLE render endpoint is not OK/Active and the multimedia default; reboot or default selection may be required' }

    $drivers = @(Get-CimInstance Win32_PnPSignedDriver | Where-Object {
        $_.HardWareID -eq 'VBAudioVACWDM'
    })
    $evidence.drivers = @($drivers | Select-Object DeviceID, DriverVersion, InfName, IsSigned, Signer, HardWareID)
    if ($drivers.Count -ne 1 -or !$drivers[0].IsSigned -or $drivers[0].DriverVersion -ne '3.5.2.0') {
        throw 'Expected one signed Pack46 PnP driver binding (3.5.2.0)'
    }
    $device = Get-PnpDevice -InstanceId $drivers[0].DeviceID
    $problem = Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_ProblemCode'
    $evidence.driverDevice = $device | Select-Object InstanceId, Status
    $evidence.driverProblemCode = $problem.Data
    if ($device.Status -ne 'OK' -or $problem.Data -ne 0) { throw 'Driver PnP device has a problem or requires restart' }
    $loaded = Get-CimInstance Win32_SystemDriver -Filter "Name='VBAudioVACMME'"
    $evidence.loadedDriver = $loaded | Select-Object Name, State, PathName
    if (!$loaded -or $loaded.State -ne 'Running') { throw 'VB-CABLE kernel driver is not running' }

    # QMediaDevices must independently report precisely this default ID in each
    # AudioEngine test. The production engine opens that same default via QAudioSink.
    "CIRRUS_TEST_AUDIO_ENDPOINT_ID=$endpointId" >> $env:GITHUB_ENV
    $evidence.result = 'ready-for-qt-tests'
    Write-Host "Trusted Pack46 installed; active default render endpoint: $endpointId"
} catch {
    $evidence.error = $_.Exception.Message
    throw
} finally {
    $evidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'endpoint.json') -Encoding utf8
    Write-Host ($evidence | ConvertTo-Json -Depth 8)
    # Preserve Windows' actual installation diagnostics even if setup/endpoint checks fail.
    $setupLog = Join-Path $env:windir 'INF/setupapi.dev.log'
    if (Test-Path -LiteralPath $setupLog) {
        Copy-Item -LiteralPath $setupLog -Destination (Join-Path $EvidenceDirectory 'setupapi.dev.log')
    }
}
