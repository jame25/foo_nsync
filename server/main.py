import http.server
import socketserver
from socketserver import ThreadingMixIn
import hashlib
import os
import json
import logging
from pathlib import Path

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

    def handle_request(self, send_body=True):
        # Log every request
        logger.info(f"Incoming {'GET' if send_body else 'HEAD'}: {self.path}")
        range_header = self.headers.get('Range')
        if range_header:
            logger.info(f"  Range Header: {range_header}")

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
            # NEW: Dedicated artwork endpoint
            # /artwork/path/to/audio/file.flac -> searches for artwork in same directory
            import urllib.parse
            audio_path_encoded = self.path[9:]  # Remove '/artwork/'
            audio_path = urllib.parse.unquote(audio_path_encoded.split('?')[0])

            logger.info(f"[ARTWORK] Raw path from URL: {audio_path_encoded}")
            logger.info(f"[ARTWORK] Decoded path: {audio_path}")

            # Ensure path starts with / (absolute path)
            if not audio_path.startswith('/'):
                audio_path = '/' + audio_path
                logger.info(f"[ARTWORK] Added leading slash: {audio_path}")

            try:
                audio_file = Path(audio_path)
                logger.info(f"[ARTWORK] Looking for audio file: {audio_file}")
                logger.info(f"[ARTWORK] Audio file exists: {audio_file.exists()}")

                if audio_file.exists() and audio_file.is_file():
                    logger.info(f"[ARTWORK] Audio file found, searching for artwork in: {audio_file.parent}")

                    # List directory contents for debugging
                    try:
                        dir_contents = list(audio_file.parent.iterdir())
                        logger.info(f"[ARTWORK] Directory contains {len(dir_contents)} items:")
                        for item in dir_contents[:20]:  # Limit to first 20
                            logger.info(f"[ARTWORK]   - {item.name}")
                    except Exception as e:
                        logger.error(f"[ARTWORK] Failed to list directory: {e}")

                    artwork = find_artwork_in_directory(audio_file.parent)
                    if artwork:
                        logger.info(f"[ARTWORK] SUCCESS - Found artwork: {artwork}")
                        with open(artwork, 'rb') as f:
                            content = f.read()

                        mime_type = mimetypes.guess_type(str(artwork))[0] or 'image/jpeg'
                        logger.info(f"[ARTWORK] Serving {len(content)} bytes, mime: {mime_type}")

                        self.send_response(200)
                        self.send_header('Content-type', mime_type)
                        self.send_header('Content-Length', str(len(content)))
                        self.send_header('Cache-Control', 'public, max-age=86400')
                        self.end_headers()

                        if send_body:
                            self.wfile.write(content)
                        return
                    else:
                        logger.warning(f"[ARTWORK] No artwork found in {audio_file.parent}")
                else:
                    logger.warning(f"[ARTWORK] Audio file does NOT exist: {audio_file}")

                self.send_error(404, "Artwork not found")
            except Exception as e:
                logger.error(f"[ARTWORK] Exception: {e}", exc_info=True)
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
                logger.info(f"Artwork query parameter detected for: {audio_path}")
                
                try:
                    audio_file = Path(audio_path)
                    if audio_file.exists() and audio_file.is_file():
                        artwork = find_artwork_in_directory(audio_file.parent)
                        if artwork:
                            logger.info(f"  -> Serving artwork: {artwork}")
                            with open(artwork, 'rb') as f:
                                content = f.read()
                            
                            self.send_response(200)
                            self.send_header('Content-type', mimetypes.guess_type(str(artwork))[0] or 'image/jpeg')
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
            import os
            
            # Remove query string if any
            path_no_query = path.split('?')[0]
            clean_path = urllib.parse.unquote(path_no_query[7:]) # Strip /stream
            p = Path(clean_path)
            
            logger.info(f"Resolving: input='{path}', clean='{clean_path}'")
            
            # If exact path exists, return it
            if p.exists():
                logger.info("  -> Found Exact")
                return str(p)
                
            # Try case-insensitive lookup
            # This handles cover.jpg vs Cover.jpg vs COVER.JPG differences
            try:
                parent = p.parent
                if parent.exists() and parent.is_dir():
                    target_name = p.name.lower()
                    for item in parent.iterdir():
                        if item.name.lower() == target_name:
                            logger.info(f"  -> Found Case-Insensitive: {item}")
                            return str(item)
            except Exception as e:
                self.log_error(f"Path resolution error for {clean_path}: {e}")
                
            logger.warning(f"  -> Not Found. Parent exists? {p.parent.exists()}")
            return clean_path
        
        # Fallback for other files
        return super().translate_path(path)

class ThreadingHTTPServer(ThreadingMixIn, socketserver.TCPServer):
    """Handle requests in separate threads for concurrent streaming + artwork"""
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    logger.info("foo_nsync Server v1.0")
    logger.info(f"Config directory: {CONFIG_DIR}")
    logger.info(f"Playlist directory: {PLAYLIST_DIR}")
    logger.info(f"Bind address: {BIND_ADDRESS}:{PORT}")
    logger.info("Endpoints: /status, /list, /hash/{name}, /playlist/{name}, /artwork/{path}")
    
    # Generate playlists on startup
    try:
        from generate_playlists import load_config as load_generator_config, process_sources
        generator_config = load_generator_config()
        if generator_config.get("sources"):
            logger.info("Generating playlists on startup...")
            updated = process_sources(generator_config)
            logger.info(f"Startup generation complete: {updated} playlist(s) updated")
        else:
            logger.info("No playlist sources configured, skipping startup generation")
    except ImportError:
        logger.warning("generate_playlists.py not found, skipping startup generation")
    except Exception as e:
        logger.error(f"Error during startup playlist generation: {e}")
    
    with ThreadingHTTPServer((BIND_ADDRESS, PORT), SyncHandler) as httpd:
        logger.info("Server is multi-threaded - can handle concurrent requests")
        httpd.serve_forever()
