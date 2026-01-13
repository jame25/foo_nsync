#!/usr/bin/env python3
"""
Playlist Generator for NSync Server
Monitors directories and generates .m3u8 playlists automatically.

Usage:
  python generate_playlists.py                    # Run once
  python generate_playlists.py --watch            # Run continuously
  python generate_playlists.py --config config.json

Environment Variables:
  CONFIG_DIR    - Directory containing config.json (default: /config)
  PLAYLIST_DIR  - Output directory for .m3u8 files (default: /data)
"""

import os
import sys
import json
import time
import hashlib
import argparse
import logging
from pathlib import Path
from typing import List, Dict, Optional

# Configuration
CONFIG_DIR = os.environ.get("CONFIG_DIR", "/config")
PLAYLIST_DIR = os.environ.get("PLAYLIST_DIR", "/data")
WATCH_INTERVAL = int(os.environ.get("WATCH_INTERVAL", 300))  # 5 minutes default

# Supported audio extensions
AUDIO_EXTENSIONS = {'.flac', '.mp3', '.m4a', '.ogg', '.opus', '.wav', '.aac', '.wma', '.ape', '.alac'}

# Common artwork filenames (in priority order)
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

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


def load_config() -> Dict:
    """Load configuration from config.json."""
    config_file = Path(CONFIG_DIR) / "config.json"
    
    if not config_file.exists():
        logger.warning(f"No config.json found in {CONFIG_DIR}, using defaults")
        return {"sources": []}
    
    try:
        with open(config_file, 'r') as f:
            config = json.load(f)
            logger.info(f"Loaded config from {config_file}")
            return config
    except Exception as e:
        logger.error(f"Failed to load config: {e}")
        return {"sources": []}


def find_artwork(directory: Path) -> Optional[Path]:
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


def scan_directory(directory: str, recursive: bool = True) -> List[str]:
    """Scan a directory for audio files and return sorted list of paths."""
    files = []
    dir_path = Path(directory)
    
    if not dir_path.exists():
        logger.warning(f"Directory does not exist: {directory}")
        return files
    
    pattern = '**/*' if recursive else '*'
    
    for file_path in dir_path.glob(pattern):
        if file_path.is_file() and file_path.suffix.lower() in AUDIO_EXTENSIONS:
            files.append(str(file_path))
    
    files.sort()
    return files


def generate_playlist(name: str, files: List[str], output_dir: str, include_artwork: bool = True,
                     extimg_tags: bool = True, generate_sidecar: bool = False) -> bool:
    """Generate an m3u8 playlist file with metadata and artwork references.
    
    Args:
        name: Playlist name
        files: List of audio file paths
        output_dir: Output directory for playlist
        include_artwork: Whether to include artwork metadata
        extimg_tags: Include #EXTIMG tags (for compatible players)
        generate_sidecar: Generate .artwork files next to each track (for foobar2000)
    """
    output_path = Path(output_dir) / f"{name}.m3u8"
    
    import urllib.parse
    
    # Start with M3U8 header
    playlist_lines = ["#EXTM3U"]
    
    # Track artwork files found for logging
    artwork_found_count = 0
    last_artwork_path = None
    
    for f in files:
        file_path = Path(f)
        
        # Try to find artwork in the same directory
        artwork_path = None
        if include_artwork:
            # Only search if we haven't already found artwork for this directory
            if last_artwork_path is None or last_artwork_path.parent != file_path.parent:
                artwork_path = find_artwork(file_path.parent)
                if artwork_path:
                    last_artwork_path = artwork_path
                    artwork_found_count += 1
            else:
                artwork_path = last_artwork_path
        
        # Add artwork reference if found (using #EXTIMG extension)
        if artwork_path and extimg_tags:
            quoted_artwork = urllib.parse.quote(str(artwork_path))
            playlist_lines.append(f"#EXTIMG:/stream{quoted_artwork}")
        
        # Add track info
        # Use filename without extension as track name
        track_name = file_path.stem
        playlist_lines.append(f"#EXTINF:-1,{track_name}")
        
        # Add the actual file URL
        quoted_file = urllib.parse.quote(f)
        playlist_lines.append(f"/stream{quoted_file}")
        
        # Generate artwork sidecar file for foobar2000
        # This creates a .artwork file next to each track with the artwork URL
        if artwork_path and generate_sidecar:
            try:
                sidecar_path = file_path.with_suffix(file_path.suffix + '.artwork')
                artwork_url = f"/stream{urllib.parse.quote(str(artwork_path))}"
                if not sidecar_path.exists() or sidecar_path.read_text().strip() != artwork_url:
                    sidecar_path.write_text(artwork_url)
            except Exception as e:
                logger.debug(f"Could not create sidecar for {file_path}: {e}")
    
    new_content = '\n'.join(playlist_lines) + '\n'
    
    # Check if content changed
    content_changed = True
    if output_path.exists():
        try:
            with open(output_path, 'r') as existing_file:
                existing_content = existing_file.read()
            if existing_content == new_content:
                logger.debug(f"Playlist '{name}' unchanged ({len(files)} files)")
                content_changed = False
        except Exception as e:
            logger.warning(f"Could not read existing playlist '{name}': {e}")
    
    if not content_changed:
        return False
    
    # Write new playlist
    try:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(new_content)
        
        artwork_msg = f" ({artwork_found_count} directories with artwork)" if include_artwork else ""
        logger.info(f"Generated playlist '{name}' with {len(files)} files{artwork_msg}")
        return True
    except Exception as e:
        logger.error(f"Failed to write playlist '{name}': {e}")
        return False


def process_sources(config: Dict) -> int:
    """Process all configured sources and generate playlists."""
    sources = config.get("sources", [])
    output_dir = config.get("playlist_dir", PLAYLIST_DIR)
    include_artwork = config.get("include_artwork", True)
    updated_count = 0
    
    if not sources:
        logger.warning("No sources configured. Add sources to config.json")
        logger.info("Example config.json:")
        logger.info(json.dumps({
            "playlist_dir": "/data",
            "include_artwork": True,
            "sources": [
                {"name": "music", "path": "/mnt/Music", "recursive": True},
                {"name": "music_inbox", "path": "/mnt/Music_Inbox", "recursive": True}
            ]
        }, indent=2))
        return 0
    
    for source in sources:
        name = source.get("name")
        path = source.get("path")
        recursive = source.get("recursive", True)
        
        if not name or not path:
            logger.warning(f"Invalid source config: {source}")
            continue
        
        logger.debug(f"Scanning {path} for {name}...")
        files = scan_directory(path, recursive)
        
        if not files:
            logger.warning(f"No audio files found in {path}")
            continue
        
        if generate_playlist(name, files, output_dir, include_artwork):
            updated_count += 1
    
    return updated_count


def run_once(config: Dict):
    """Run playlist generation once."""
    logger.info("Generating playlists...")
    updated = process_sources(config)
    logger.info(f"Done. {updated} playlist(s) updated.")


def run_watch(config: Dict, interval: int):
    """Run playlist generation continuously."""
    logger.info(f"Starting watch mode (interval: {interval}s)")
    
    while True:
        try:
            process_sources(config)
        except Exception as e:
            logger.error(f"Error during processing: {e}")
        
        logger.debug(f"Sleeping for {interval}s...")
        time.sleep(interval)


def main():
    parser = argparse.ArgumentParser(description="Generate playlists from directories")
    parser.add_argument("--watch", "-w", action="store_true", help="Run continuously")
    parser.add_argument("--interval", "-i", type=int, default=WATCH_INTERVAL, 
                        help=f"Watch interval in seconds (default: {WATCH_INTERVAL})")
    parser.add_argument("--config", "-c", type=str, help="Path to config.json")
    parser.add_argument("--debug", "-d", action="store_true", help="Enable debug logging")
    parser.add_argument("--no-artwork", action="store_true", help="Disable artwork detection")
    
    args = parser.parse_args()
    
    if args.debug:
        logging.getLogger().setLevel(logging.DEBUG)
    
    # Load config
    if args.config:
        global CONFIG_DIR
        CONFIG_DIR = str(Path(args.config).parent)
    
    config = load_config()
    
    # Override artwork setting from command line
    if args.no_artwork:
        config["include_artwork"] = False
    
    if args.watch:
        run_watch(config, args.interval)
    else:
        run_once(config)


if __name__ == "__main__":
    main()
