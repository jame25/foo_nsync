import http.server
import socketserver
from socketserver import ThreadingMixIn
import hashlib
import os
import json
import logging
from pathlib import Path
from functools import lru_cache
import threading

# CONFIGURATION (via environment variables)
PORT = int(os.environ.get("PORT", 8090))
BIND_ADDRESS = os.environ.get("BIND_ADDRESS", "0.0.0.0")
PLAYLIST_DIR = os.environ.get("PLAYLIST_DIR", "/data")
CONFIG_DIR = os.environ.get("CONFIG_DIR", "/config")
LOG_LEVEL = os.environ.get("LOG_LEVEL", "INFO").upper()

# Setup logging
logging.basicConfig(
    level=getattr(logging, LOG_LEVEL, logging.INFO),
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# Common artwork filenames
ARTWORK_FILENAMES = [
    'cover.jpg', 'Cover.jpg', 'COVER.JPG',
    'folder.jpg', 'Folder.jpg', 'FOLDER.JPG',
    'front.jpg', 'Front.jpg', 'FRONT.JPG',
    'cover.png', 'Cover.png', 'COVER.PNG',
    'folder.png', 'Folder.png', 'FOLDER.PNG',
    'front.png', 'Front.png', 'FRONT.PNG',
    'album.jpg', 'Album.jpg', 'ALBUM.JPG',
    'albumart.jpg', 'AlbumArt.jpg', 'ALBUMART.JPG'
]

# Load optional config file from CONFIG_DIR
def load_config():
    """Load configuration from JSON file if it exists."""
    config_file = Path(CONFIG_DIR) / "config.json"
    if config_file.exists():
        try:
            with open(config_file, 'r') as f:
                config = json.load(f)
                logger.info(f"Loaded config from {config_file}")
                return config
        except Exception as e:
            logger.warning(f"Failed to load config file: {e}")
    return {}

# Merge file config with env vars (env vars take precedence)
file_config = load_config()
PLAYLIST_DIR = os.environ.get("PLAYLIST_DIR") or file_config.get("playlist_dir", PLAYLIST_DIR)

# Explicit MIME type registration
import mimetypes
if not mimetypes.inited:
    mimetypes.init()
mimetypes.add_type('audio/flac', '.flac')
mimetypes.add_type('audio/mpeg', '.mp3')
mimetypes.add_type('audio/mp4', '.m4a')
mimetypes.add_type('audio/ogg', '.ogg')
mimetypes.add_type('audio/x-ape', '.ape')
mimetypes.add_type('image/jpeg', '.jpg')
mimetypes.add_type('image/jpeg', '.jpeg')
mimetypes.add_type('image/png', '.png')
mimetypes.add_type('image/gif', '.gif')
mimetypes.add_type('image/bmp', '.bmp')

# Artwork cache - keyed by directory path, stores (content_bytes, mime_type)
_artwork_cache = {}
_artwork_cache_lock = threading.Lock()
ARTWORK_CACHE_MAX_SIZE = 500  # Max cached directories


def get_cached_artwork(directory: Path):
    """Get artwork from cache or load from disk. Returns (content, mime_type) or (None, None)."""
    cache_key = str(directory)

    # Check cache first (fast path)
    with _artwork_cache_lock:
        if cache_key in _artwork_cache:
            return _artwork_cache[cache_key]

    # Not in cache - find and load artwork
    artwork_path = find_artwork_in_directory(directory)
    if not artwork_path:
        # Cache negative result too (avoid repeated disk scans)
        with _artwork_cache_lock:
            if len(_artwork_cache) >= ARTWORK_CACHE_MAX_SIZE:
                # Simple eviction: clear half the cache
                keys = list(_artwork_cache.keys())[:len(_artwork_cache) // 2]
                for k in keys:
                    del _artwork_cache[k]
            _artwork_cache[cache_key] = (None, None)
        return (None, None)

    # Load artwork content
    try:
        with open(artwork_path, 'rb') as f:
            content = f.read()
        mime_type = mimetypes.guess_type(str(artwork_path))[0] or 'image/jpeg'

        # Store in cache
        with _artwork_cache_lock:
            if len(_artwork_cache) >= ARTWORK_CACHE_MAX_SIZE:
                keys = list(_artwork_cache.keys())[:len(_artwork_cache) // 2]
                for k in keys:
                    del _artwork_cache[k]
            _artwork_cache[cache_key] = (content, mime_type)

        return (content, mime_type)
    except Exception:
        return (None, None)


def find_artwork_in_directory(directory: Path):
    """Find artwork file in the given directory."""
    # First, try common artwork filenames (priority order)
    for artwork_name in ARTWORK_FILENAMES:
        artwork_path = directory / artwork_name
        if artwork_path.exists() and artwork_path.is_file():
            return artwork_path

    # Fall back to any .jpg or .png file in the directory
    for ext in ['*.jpg', '*.jpeg', '*.png']:
        for img_path in directory.glob(ext):
            if img_path.is_file():
                return img_path

    return None

class SyncHandler(http.server.SimpleHTTPRequestHandler):
    def address_string(self):
        # Skip reverse DNS lookup (causes 1-2 min delays)
        return self.client_address[0]

    def do_GET(self):
        self.handle_request(send_body=True)

    def do_HEAD(self):
        self.handle_request(send_body=False)

    def do_POST(self):
        """Handle POST requests for on-demand sync operations."""
        if self.path.startswith('/sync/'):
            playlist_name = self.path[6:]  # Remove '/sync/'

            try:
                # Load config to find the source for this playlist
                from generate_playlists import load_config as load_generator_config, incremental_update_playlist
                generator_config = load_generator_config()

                # Find the source configuration for this playlist
                source = None
                for s in generator_config.get("sources", []):
                    if s.get("name") == playlist_name:
                        source = s
                        break

                if not source:
                    self.send_response(404)
                    self.send_header('Content-type', 'application/json')
                    self.end_headers()
                    self.wfile.write(json.dumps({
                        "error": f"No source configured for playlist '{playlist_name}'"
                    }).encode())
                    return

                # Perform incremental update
                output_dir = generator_config.get("playlist_dir", PLAYLIST_DIR)
                include_artwork = generator_config.get("include_artwork", True)

                result = incremental_update_playlist(
                    name=playlist_name,
                    source_path=source.get("path"),
                    output_dir=output_dir,
                    recursive=source.get("recursive", True),
                    include_artwork=include_artwork,
                    recently_added_days=source.get("recently_added_days")
                )

                response_data = {
                    "playlist": playlist_name,
                    "updated": result["updated"],
                    "added_count": len(result["added"]),
                    "removed_count": len(result.get("removed", [])),
                    "total": result["total"],
                    "existing_count": result.get("existing_count", 0),
                    "scanned_count": result.get("scanned_count", 0),
                    "added_files": [Path(f).name for f in result["added"][:20]],
                    "removed_files": [Path(f).name for f in result.get("removed", [])[:20]],
                    "recently_added_days": source.get("recently_added_days")
                }

                # Include sample paths if no changes found (helps debug path mismatches)
                if len(result["added"]) == 0 and len(result.get("removed", [])) == 0:
                    response_data["sample_existing"] = result.get("sample_existing")
                    response_data["sample_scanned"] = result.get("sample_scanned")

                self.send_response(200)
                self.send_header('Content-type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps(response_data).encode())

            except Exception as e:
                logger.error(f"Sync error for '{playlist_name}': {e}")
                self.send_response(500)
                self.send_header('Content-type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps({"error": str(e)}).encode())
            return

        else:
            self.send_error(404, "POST endpoint not found")

    def handle_request(self, send_body=True):
        if self.path == '/status':
            self.send_response(200)
            self.end_headers()
            if send_body:
                self.wfile.write(b"OK")
            return

        elif self.path == '/list':
            # Return JSON array of available playlist names
            try:
                playlists = []
                playlist_path = Path(PLAYLIST_DIR)
                for f in playlist_path.glob("*.m3u8"):
                    playlists.append(f.stem)
                
                self.send_response(200)
                self.send_header('Content-type', 'application/json')
                self.end_headers()
                if send_body:
                    self.wfile.write(json.dumps(playlists).encode())
            except Exception as e:
                self.send_error(500, str(e))
            return

        elif self.path.startswith('/hash/'):
            # Get hash of specific playlist
            playlist_name = self.path[6:]
            playlist_file = Path(PLAYLIST_DIR) / f"{playlist_name}.m3u8"
            
            try:
                with open(playlist_file, 'rb') as f:
                    file_hash = hashlib.md5(f.read()).hexdigest()
                self.send_response(200)
                self.end_headers()
                if send_body:
                    self.wfile.write(file_hash.encode())
            except FileNotFoundError:
                self.send_error(404, f"Playlist '{playlist_name}' not found")
            return

        elif self.path.startswith('/playlist/'):
            # Download specific playlist
            playlist_name = self.path[10:]
            playlist_file = Path(PLAYLIST_DIR) / f"{playlist_name}.m3u8"
            
            try:
                with open(playlist_file, 'rb') as f:
                    content = f.read()
                self.send_response(200)
                self.send_header('Content-type', 'application/x-mpegurl')
                self.send_header('Content-Disposition', f'attachment; filename="{playlist_name}.m3u8"')
                self.end_headers()
                if send_body:
                    self.wfile.write(content)
            except FileNotFoundError:
                self.send_error(404, f"Playlist '{playlist_name}' not found")
            return

        # Legacy endpoint for backward compatibility
        elif self.path == '/hash':
            playlist_file = Path(PLAYLIST_DIR) / "master_playlist.m3u8"
            try:
                with open(playlist_file, 'rb') as f:
                    file_hash = hashlib.md5(f.read()).hexdigest()
                self.send_response(200)
                self.end_headers()
                if send_body:
                    self.wfile.write(file_hash.encode())
            except FileNotFoundError:
                self.send_error(404, "Playlist not found")
            return

        elif self.path == '/playlist':
            playlist_file = Path(PLAYLIST_DIR) / "master_playlist.m3u8"
            try:
                with open(playlist_file, 'rb') as f:
                    content = f.read()
                self.send_response(200)
                self.send_header('Content-type', 'application/x-mpegurl')
                self.send_header('Content-Disposition', 'attachment; filename="playlist.m3u8"')
                self.end_headers()
                if send_body:
                    self.wfile.write(content)
            except FileNotFoundError:
                self.send_error(404, "Playlist not found")
            return
        
        elif self.path.startswith('/artwork/'):
            # Dedicated artwork endpoint
            # /artwork/path/to/audio/file.flac -> searches for artwork in same directory
            import urllib.parse
            audio_path_encoded = self.path[9:]  # Remove '/artwork/'
            audio_path = urllib.parse.unquote(audio_path_encoded.split('?')[0])

            # Ensure path starts with / (absolute path)
            if not audio_path.startswith('/'):
                audio_path = '/' + audio_path

            try:
                audio_file = Path(audio_path)

                if audio_file.exists() and audio_file.is_file():
                    # Use cached artwork lookup
                    content, mime_type = get_cached_artwork(audio_file.parent)
                    if content:
                        self.send_response(200)
                        self.send_header('Content-type', mime_type)
                        self.send_header('Content-Length', str(len(content)))
                        self.send_header('Cache-Control', 'public, max-age=86400')
                        self.end_headers()

                        if send_body:
                            self.wfile.write(content)
                        return

                self.send_error(404, "Artwork not found")
            except Exception as e:
                logger.error(f"Artwork error: {e}")
                self.send_error(500, str(e))
            return

        elif self.path.startswith('/stream/'):
            # Check if this is an artwork request disguised as a stream request
            # Some players append ?artwork or use special extensions
            import urllib.parse
            path_no_query = self.path.split('?')[0]
            query_params = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)

            # Check if artwork is explicitly requested via query parameter
            if 'artwork' in query_params or 'cover' in query_params or 'art' in query_params:
                audio_path = urllib.parse.unquote(path_no_query[7:])  # Remove /stream/

                try:
                    audio_file = Path(audio_path)
                    if audio_file.exists() and audio_file.is_file():
                        content, mime_type = get_cached_artwork(audio_file.parent)
                        if content:
                            self.send_response(200)
                            self.send_header('Content-type', mime_type)
                            self.send_header('Content-Length', str(len(content)))
                            self.send_header('Cache-Control', 'public, max-age=86400')
                            self.end_headers()

                            if send_body:
                                self.wfile.write(content)
                            return
                except Exception as e:
                    logger.error(f"Artwork query error: {e}")
            
            # Serve file with Range support (existing logic)
            f = None
            try:
                # Resolve path
                path = self.translate_path(self.path)
                p = Path(path)
                if not p.exists() or p.is_dir():
                    self.send_error(404, "File not found")
                    return

                f = open(path, 'rb')
                fs = os.fstat(f.fileno())
                file_len = fs.st_size
                
                # Parse Range header
                range_header = self.headers.get('Range')
                start, end = 0, file_len - 1
                
                if range_header:
                    try:
                        _, r = range_header.split('=')
                        r_start, r_end = r.split('-')
                        start = int(r_start) if r_start else 0
                        end = int(r_end) if r_end else file_len - 1
                    except ValueError:
                        pass # Ignore invalid range
                        
                    if start >= file_len:
                        self.send_error(416, "Requested Range Not Satisfiable")
                        self.send_header("Content-Range", f"bytes */{file_len}")
                        self.end_headers()
                        return
                        
                    self.send_response(206)
                    self.send_header("Content-Range", f"bytes {start}-{end}/{file_len}")
                else:
                    self.send_response(200)

                self.send_header("Accept-Ranges", "bytes")
                self.send_header("Content-type", mimetypes.guess_type(path)[0] or 'application/octet-stream')
                self.send_header("Content-Length", str(end - start + 1))
                self.send_header("Last-Modified", self.date_time_string(fs.st_mtime))
                self.end_headers()
                
                if not send_body:
                    return

                # Send data
                f.seek(start)
                left = end - start + 1
                BLOCK_SIZE = 64 * 1024
                
                while left > 0:
                    block = f.read(min(BLOCK_SIZE, left))
                    if not block:
                        break
                    try:
                        self.wfile.write(block)
                    except (ConnectionResetError, BrokenPipeError):
                        break
                    left -= len(block)
                    
            except Exception as e:
                self.log_error(f"Stream error: {e}")
            finally:
                if f:
                    f.close()
            return
            
        else:
            self.send_error(404)


    def translate_path(self, path):
        """Map /stream/X to absolute path /X with case-insensitive fallback"""
        if path.startswith('/stream/'):
            # Decode URL path
            import urllib.parse

            # Remove query string if any
            path_no_query = path.split('?')[0]
            clean_path = urllib.parse.unquote(path_no_query[7:])  # Strip /stream
            p = Path(clean_path)

            # If exact path exists, return it
            if p.exists():
                return str(p)

            # Try case-insensitive lookup
            # This handles cover.jpg vs Cover.jpg vs COVER.JPG differences
            try:
                parent = p.parent
                if parent.exists() and parent.is_dir():
                    target_name = p.name.lower()
                    for item in parent.iterdir():
                        if item.name.lower() == target_name:
                            return str(item)
            except Exception as e:
                self.log_error(f"Path resolution error for {clean_path}: {e}")

            return clean_path

        # Fallback for other files
        return super().translate_path(path)

class ThreadingHTTPServer(ThreadingMixIn, socketserver.TCPServer):
    """Handle requests in separate threads for concurrent streaming + artwork"""
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    logger.info("foo_nsync Server v1.0.3")
    logger.info(f"Config directory: {CONFIG_DIR}")
    logger.info(f"Playlist directory: {PLAYLIST_DIR}")
    logger.info(f"Bind address: {BIND_ADDRESS}:{PORT}")
    logger.info("Endpoints: /status, /list, /hash/{name}, /playlist/{name}, /artwork/{path}, POST /sync/{name}")
    
    # Check playlists on startup - only create if missing, never full regenerate
    try:
        from generate_playlists import load_config as load_generator_config, generate_playlist, scan_directory
        generator_config = load_generator_config()
        sources = generator_config.get("sources", [])
        output_dir = generator_config.get("playlist_dir", PLAYLIST_DIR)
        include_artwork = generator_config.get("include_artwork", True)

        if sources:
            for source in sources:
                name = source.get("name")
                path = source.get("path")
                recursive = source.get("recursive", True)

                if not name or not path:
                    continue

                playlist_file = Path(output_dir) / f"{name}.m3u8"
                recently_added_days = source.get("recently_added_days")

                if not playlist_file.exists():
                    # Only generate if playlist doesn't exist
                    logger.info(f"Creating missing playlist '{name}'...")
                    files = scan_directory(path, recursive, recently_added_days)
                    if files:
                        generate_playlist(name, files, output_dir, include_artwork)
                else:
                    logger.info(f"Playlist '{name}' exists, skipping startup generation")
        else:
            logger.info("No playlist sources configured")
    except ImportError:
        logger.warning("generate_playlists.py not found, skipping startup check")
    except Exception as e:
        logger.error(f"Error during startup playlist check: {e}")
    
    with ThreadingHTTPServer((BIND_ADDRESS, PORT), SyncHandler) as httpd:
        logger.info("Server is multi-threaded - can handle concurrent requests")
        httpd.serve_forever()
