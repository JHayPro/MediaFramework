$url = "https://github.com/GyanD/codexffmpeg/releases/download/8.0.1/ffmpeg-8.0.1-full_build-shared.zip"

if (!(Test-Path "ffmpeg.zip")) {
    Invoke-WebRequest -Uri $url -OutFile "ffmpeg.zip"
}

Expand-Archive -Path "ffmpeg.zip" -DestinationPath "external/FFmpeg" -Force