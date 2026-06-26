$exeFiles = @(
    @{
        Path = ".\dawn\app-d3d12.exe"
        Args = @("native-lite-benchmark.lite.js", "--width", "640", "--height", "400", "--no-vsync", "--frames", "1000")
    },
    @{
        Path = ".\dawn\app-d3d11.exe"
        Args = @("native-lite-benchmark.lite.js", "--width", "640", "--height", "400", "--no-vsync", "--frames", "1000")
    },
    @{
        Path = ".\bgfx\app-d3d12.exe"
        Args = @("native-lite-benchmark.lite.js", "--width", "640", "--height", "400", "--no-vsync", "--frames", "1000")
    },
    @{
        Path = ".\bgfx\app-d3d11.exe"
        Args = @("native-lite-benchmark.lite.js", "--width", "640", "--height", "400", "--no-vsync", "--frames", "1000")
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