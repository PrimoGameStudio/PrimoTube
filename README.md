# PrimoTube

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/A3D729DKXZ)

A modern YouTube client built with Qt 6 and QML. Search videos, play them in
HD via embedded libmpv (with subtitles), download them with yt-dlp, and keep
a local watch history and favorites list.

## Features

- YouTube search with paged results (up to 50, 10 per page)
- HD playback (DASH, up to 4K) via embedded **libmpv**, with subtitle and
  audio track selection; automatic fallback to QtMultimedia progressive
  streams when libmpv is unavailable, plus an option to **always use the
  built-in player** (lower CPU/GPU on old hardware or slow connections)
- Video details: description, view/like counts, upload date, subtitle languages
- Downloads (best quality, merged to MP4 via ffmpeg)
- Watch history (100 items) and favorites, persisted locally
- yt-dlp update check with in-app update banner
- Configurable quality cap (Auto / 720p / 1080p / 4K)
- MPRIS support (`org.mpris.MediaPlayer2.primotube`): metadata and
  Play/Pause/Stop/Seek/Volume for desktop media widgets on both the mpv
  and fallback playback paths

## Dependencies

- Qt 6.5+ (Quick, Multimedia, Network, OpenGL)
- CMake 3.21+, a C++17 compiler, pkg-config
- [yt-dlp](https://github.com/yt-dlp/yt-dlp) in `PATH` (search, playback, downloads)
- [ffmpeg](https://ffmpeg.org/) in `PATH` (downloads only)
- libmpv with headers (`mpv`/`mpv-devel` package) — optional but recommended;
  without it the app falls back to SD progressive playback

On Arch Linux: `sudo pacman -S qt6-base qt6-multimedia qt6-declarative cmake yt-dlp ffmpeg mpv`

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Run: `./build/primotube`

## Tests

```sh
ctest --test-dir build --output-on-failure
```

## Notes

- On Wayland the app uses the OpenGL RHI backend for mpv rendering.
- Settings → Playback → *Always use the built-in player* forces the
  QtMultimedia path for every video and switches the currently playing
  video immediately. The built-in player only handles progressive streams
  (usually SD), so the quality cap, subtitle and audio-track selection do
  not apply there.
- libmpv requires the C numeric locale; the app sets it on startup.
- Quality caps apply to the next video loaded.
- History entries are added once playback starts successfully, so failed
  lookups don't pollute the list.

## License

MIT — see [LICENSE](LICENSE).
