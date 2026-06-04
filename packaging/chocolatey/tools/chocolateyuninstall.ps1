$ErrorActionPreference = 'Stop'

# Uninstall by the MSI ProductCode of v0.1.0 (msiexec /x). When newer versions
# ship, prefer resolving the code at runtime from the registry so the uninstall
# tracks whatever version is actually installed.
$productCode = '{722843FB-8862-4EAB-9640-AF078C3B550D}'

$key = Get-UninstallRegistryKey -SoftwareName 'ajazz-control-center*'
if ($key -and $key.Count -eq 1) {
  $productCode = $key.PSChildName
}

$packageArgs = @{
  packageName    = 'ajazz-control-center'
  fileType       = 'msi'
  silentArgs     = "$productCode /qn /norestart"
  validExitCodes = @(0, 3010, 1605, 1614, 1641)  # 1605/1614 = already absent
}

Uninstall-ChocolateyPackage @packageArgs
