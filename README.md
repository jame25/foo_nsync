# foo_nsync (Playlist Sync)

A native **foobar2000 component** that synchronizes playlists from a remote server and enables direct HTTP streaming playback with album art support.

`foo_nsync` is designed to solve the problem of playing your home music collection remotely without complex VPNs, SMB shares, or manual playlist management. It treats the server as the "Source of Truth" for playlists and streams the audio files directly over HTTP.

## Features

*   **Remote Playlist Synchronization**: Automatically pulls `.m3u8` playlists from a remote server.
*   **Zero-Config Streaming**: No need to map network drives. The component automatically detects server paths and streams them via HTTP.
*   **Album Art Support**: Automatically fetches and displays album art in foobar2000's Default UI artwork panel.
*   **Smart Updates**: Only downloads playlists when the server hash changes (bandwidth efficient).
*   **Resilience**: Automatically detects if a playlist is missing in foobar2000 and restores it.
*   **Progress UI**: Real-time status updates ("Checking...", "Downloading...", "Updating...") in Preferences.

## Architecture

The system consists of two parts:

### Server (`server`)

A Python-based multi-threaded HTTP service that:
*   Generates `.m3u8` playlists from your music directories.
*   Exposes an HTTP API for playlist metadata and content.
*   Serves audio files via the `/stream/` endpoint.
*   Serves album art via the `/artwork/` endpoint.

### Client (`foo_nsync.dll`)

A C++ component for foobar2000 that:
*   Polls the server for playlist changes.
*   Downloads playlists and adds them to foobar2000.
*   Implements `album_art_extractor` and `album_art_fallback` services for artwork display.
*   Transparently handles playback URL construction.

## Server API Endpoints

| Endpoint | Description |
|----------|-------------|
| `GET /status` | Health check, returns "OK" |
| `GET /list` | Returns JSON array of available playlist names |
| `GET /hash/{name}` | Returns MD5 hash of playlist (for change detection) |
| `GET /playlist/{name}` | Downloads the .m3u8 playlist file |
| `GET /stream/{path}` | Streams an audio file (supports Range requests) |
| `GET /artwork/{path}` | Returns album art for the audio file's directory |

## Installation

### 1. Server Setup

Navigate to the `server` directory.

**Prerequisites**: Python 3.9+ (or Docker & Docker Compose).

#### Option A: Run directly
```bash
cd server
python main.py
```

#### Option B: Docker

1. Copy the example files:
   ```bash
   cp .env.example .env
   cp config.example.json config/config.json
   ```

2. Edit `.env` and set your music directory paths:
   ```bash
   MUSIC_DIR_1=/path/to/music
   MUSIC_DIR_2=/path/to/more/music
   MUSIC_DIR_3=/path/to/even/more/music
   ```

3. Edit `config/config.json` to match your sources:
   ```json
   {
       "playlist_dir": "/data",
       "sources": [
           {"name": "music", "path": "/mnt/Music1", "recursive": true},
           {"name": "more_music", "path": "/mnt/Music2", "recursive": true},
           {"name": "even_more_music", "path": "/mnt/Music3", "recursive": true}
       ]
   }
   ```

4. **Important**: If you have fewer than 3 music directories, comment out the unused `MUSIC_DIR_*` lines in `.env` **and** the corresponding volume mounts in `docker-compose.yml`.

5. Start the server:
   ```bash
   docker-compose up -d --build
   ```

Ensure port `8090` (default) is accessible from your client machine.

### 2. Client Setup (Build from Source)

**Prerequisites**: Visual Studio 2022, foobar2000 SDK, WTL.

1.  Open `foo_nsync.sln` in Visual Studio.
2.  Select **Release** configuration and **x64** platform.
3.  Build the solution.
4.  Copy the output `foo_nsync.dll` to your foobar2000 `components` directory.
5.  Restart foobar2000.

## Configuration

1.  Open foobar2000 **Preferences** (`Ctrl+P`).
2.  Navigate to **Tools > Playlist Sync**.
3.  Click **Add** to create a new sync job.
4.  Fill in the details:
    *   **Server URL**: `http://<server-ip>:8090` (e.g., `http://192.168.1.50:8090`)
    *   **Playlist Name**: The name of the playlist file on the server *without extension* (e.g., `music`).
    *   **Target Playlist**: The name you want it to appear as in foobar2000.
    *   **Enable**: Check this box.
5.  Click **OK**, then **Apply**.
6.  Click **Sync Now** to test the connection.

## Album Art

Album art is automatically fetched when playing tracks from the server. The server searches for artwork in the same directory as the audio file, looking for:

1.  Common filenames first: `cover.jpg`, `folder.jpg`, `front.jpg`, `album.jpg`, etc.
2.  Falls back to any `.jpg`, `.jpeg`, or `.png` file in the directory.

To display artwork in foobar2000:
1.  Enable the Album Art panel: **View > Default UI > Album Art**
2.  Play a track from a synced playlist

## Troubleshooting

### Connection Issues
*   **"Request failed" errors**: Ensure the server is running and accessible. Try opening `http://<server-ip>:8090/status` in a browser.
*   **Slow initial response**: The server may need a moment on first request. Subsequent requests should be fast.

### Streaming Issues
*   **Files not playing**: Check if the audio file path exists on the server.
*   **Empty Playlist**: Ensure playlists were generated. Check server logs.

### Album Art Issues
*   **No artwork displayed**: Verify artwork exists in the album folder on the server.
*   **Test artwork endpoint**: Open `http://<server-ip>:8090/artwork/<path-to-audio-file>` in a browser.
*   **Check console**: Open foobar2000's View > Console for `foo_nsync [art]:` messages.


## Environment Variables (Server)

| Variable | Default | Description |
|----------|---------|-------------|
| `PORT` | `8090` | Server listening port |
| `BIND_ADDRESS` | `0.0.0.0` | Bind address |
| `PLAYLIST_DIR` | `/data` | Output directory for playlists |
| `CONFIG_DIR` | `/config` | Configuration directory |
| `LOG_LEVEL` | `INFO` | Logging verbosity |

## License

[MIT License](LICENSE)
