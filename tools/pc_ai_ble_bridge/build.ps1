[CmdletBinding()]
param(
    [switch]$AllowOverwrite
)

$ErrorActionPreference = 'Stop'
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourcePath = Join-Path $scriptDirectory 'Program.cs'
$outputDirectory = Join-Path $scriptDirectory 'bin'
$outputPath = Join-Path $outputDirectory 'PcAiBleBridge.exe'
$frameworkRoot = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319'
$compilerPath = Join-Path $frameworkRoot 'csc.exe'
$runtimePath = Join-Path $frameworkRoot 'System.Runtime.WindowsRuntime.dll'
$metadataRoot = 'C:\Windows\System32\WinMetadata'
$references = @(
    $runtimePath,
    (Join-Path $frameworkRoot 'System.Runtime.InteropServices.WindowsRuntime.dll'),
    (Join-Path $frameworkRoot 'System.Runtime.dll'),
    (Join-Path $frameworkRoot 'System.Threading.Tasks.dll'),
    (Join-Path $metadataRoot 'Windows.Foundation.winmd'),
    (Join-Path $metadataRoot 'Windows.Devices.winmd'),
    (Join-Path $metadataRoot 'Windows.Storage.winmd')
)

if (-not (Test-Path -LiteralPath $compilerPath -PathType Leaf)) {
    throw "The .NET Framework compiler was not found at $compilerPath"
}

if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "The source file was not found at $sourcePath"
}

foreach ($reference in $references) {
    if (-not (Test-Path -LiteralPath $reference -PathType Leaf)) {
        throw "A required system reference was not found at $reference"
    }
}

if (Test-Path -LiteralPath $outputPath -PathType Leaf) {
    if (-not $AllowOverwrite) {
        throw "The output already exists. Re-run with -AllowOverwrite to replace it: $outputPath"
    }
}

if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
}

$compilerArguments = @(
    '/nologo',
    '/target:exe',
    '/platform:anycpu',
    '/optimize+',
    '/langversion:5',
    "/out:$outputPath"
)

foreach ($reference in $references) {
    $compilerArguments += "/reference:$reference"
}

$compilerArguments += $sourcePath
& $compilerPath @compilerArguments

if ($LASTEXITCODE -ne 0) {
    throw "Compilation failed with exit code $LASTEXITCODE"
}

Write-Host "Built $outputPath"
