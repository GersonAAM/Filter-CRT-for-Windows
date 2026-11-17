# RetroCRT - instala la version UIAccess, que filtra tambien el menu Inicio y los paneles del
# sistema (Wi-Fi, Bluetooth, volumen, notificaciones).
#
# Windows solo concede UIAccess a un exe que cumpla dos condiciones:
#   1. Esta firmado con un certificado en el que confia el equipo.
#   2. Esta en una carpeta protegida (Program Files).
# Este script crea un certificado de firma local, firma el exe y deja como de confianza SOLO la
# parte publica del certificado. La clave privada se destruye al terminar, asi que nadie puede
# usar ese certificado para firmar otro programa.
#
#   Instalar:     clic derecho > Ejecutar con PowerShell      (pide permisos de administrador)
#   Desinstalar:  powershell -ExecutionPolicy Bypass -File install-uiaccess.ps1 -Uninstall

param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    if ($Uninstall) { $argList += ' -Uninstall' }
    Start-Process powershell.exe -Verb RunAs -ArgumentList $argList
    exit
}

$root     = Split-Path -Parent $PSCommandPath
$source   = Join-Path $root 'bin\RetroCRT_UIAccess.exe'
$destDir  = Join-Path $env:ProgramFiles 'RetroCRT'
$destExe  = Join-Path $destDir 'RetroCRT.exe'
$subject  = 'CN=RetroCRT Local Code Signing'
$shortcut = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'RetroCRT.lnk'

function Remove-RetroCrtCertificates {
    foreach ($store in 'Cert:\LocalMachine\Root', 'Cert:\LocalMachine\TrustedPublisher') {
        Get-ChildItem $store | Where-Object { $_.Subject -eq $subject } | Remove-Item
    }
}

function Stop-RetroCrt {
    Get-Process -Name 'RetroCRT', 'RetroCRT_UIAccess' -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
}

try {
    if ($Uninstall) {
        Stop-RetroCrt
        Remove-Item $destDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item $shortcut -Force -ErrorAction SilentlyContinue
        Remove-RetroCrtCertificates
        Write-Host 'RetroCRT (UIAccess) desinstalado. config.json sigue en %APPDATA%\RetroCRT.' -ForegroundColor Green
        return
    }

    if (-not (Test-Path $source)) { throw "No existe $source. Ejecuta build.bat primero." }

    Write-Host '1/4 Copiando a Program Files...'
    Stop-RetroCrt
    New-Item -ItemType Directory -Force $destDir | Out-Null
    Copy-Item $source $destExe -Force

    Write-Host '2/4 Creando certificado de firma local...'
    Remove-RetroCrtCertificates # certificados de instalaciones anteriores
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject `
        -CertStoreLocation 'Cert:\CurrentUser\My' -KeyExportPolicy NonExportable -NotAfter (Get-Date).AddYears(10)
    $cerFile = Join-Path $env:TEMP 'RetroCRT-signing.cer'
    try {
        Write-Host '3/4 Firmando RetroCRT.exe...'
        Set-AuthenticodeSignature -FilePath $destExe -Certificate $cert -HashAlgorithm SHA256 | Out-Null
        Export-Certificate -Cert $cert -FilePath $cerFile | Out-Null
        Import-Certificate -FilePath $cerFile -CertStoreLocation 'Cert:\LocalMachine\Root' | Out-Null
        Import-Certificate -FilePath $cerFile -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher' | Out-Null
    } finally {
        # Destruye la clave privada: el certificado ya no puede firmar nada mas.
        Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey -ErrorAction SilentlyContinue
        Remove-Item $cerFile -Force -ErrorAction SilentlyContinue
    }

    $status = (Get-AuthenticodeSignature $destExe).Status
    if ($status -ne 'Valid') { throw "La firma no es valida ($status)." }

    Write-Host '4/4 Creando acceso directo en el menu Inicio...'
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut($shortcut)
    $link.TargetPath = $destExe
    $link.WorkingDirectory = $destDir
    $link.Description = 'RetroCRT - filtro CRT en tiempo real'
    $link.Save()

    Write-Host ''
    Write-Host "Instalado en $destExe" -ForegroundColor Green
    Write-Host 'La configuracion se guarda en %APPDATA%\RetroCRT\config.json'
    Write-Host 'Abrelo desde el menu Inicio (RetroCRT). Cierra antes la version portable si esta abierta.'
} catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
} finally {
    Read-Host 'Pulsa Enter para cerrar'
}
