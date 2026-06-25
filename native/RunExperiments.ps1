$exeFiles = @(
    @{
        Path = ".\dawn\app.exe"
        Args = @("native-lite-benchmark.lite.js", "--no-vsync", "--frames", "1000")
    }
)

foreach ($exe in $exeFiles) {
    $exePath = Resolve-Path $exe.Path
    $exeDir  = Split-Path $exePath -Parent
    $exeName = Split-Path $exePath -Leaf

    Write-Host "Running $exeName $($exe.Args -join ' ') in $exeDir"

    Push-Location $exeDir
    try {
        & ".\$exeName" @($exe.Args)
    }
    finally {
        Pop-Location
    }
}