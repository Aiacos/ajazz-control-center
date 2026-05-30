$ErrorActionPreference = 'Stop'

# Installs the official AJAZZ Control Center MSI published on GitHub Releases.
# The checksum is the SHA256 of the v0.1.0 win64 MSI (matches the release
# SHA256SUMS); Chocolatey verifies the download against it before installing.
$packageArgs = @{
  packageName    = 'ajazz-control-center'
  fileType       = 'msi'
  url64bit       = 'https://github.com/Aiacos/ajazz-control-center/releases/download/v0.1.0/ajazz-control-center-0.1.0-win64.msi'
  checksum64     = '3A91E4A5C438BB2C705941888431F8DB8D76D4C11D6986F1D5AD3737289424BA'
  checksumType64 = 'sha256'
  # /qn quiet, no UI; /norestart so packaging never forces a reboot.
  silentArgs     = '/qn /norestart'
  validExitCodes = @(0, 3010, 1641)  # 3010/1641 = success, reboot required/initiated
}

Install-ChocolateyPackage @packageArgs
