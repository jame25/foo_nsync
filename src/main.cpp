#include "stdafx.h"

// Component version information
DECLARE_COMPONENT_VERSION(
    "Playlist Sync",
    "1.0.3",
    "Syncs playlists from a remote server.\n\n"
    "Configure sync jobs in Preferences > Tools > Playlist Sync.\n\n"
    "Server endpoints:\n"
    "  /status - Health check\n"
    "  /list - Available playlists\n"
    "  /hash/{name} - Playlist hash\n"
    "  /playlist/{name} - Download playlist\n\n"
    "https://github.com/jame25/foo_nsync"
);

// Prevent renaming the DLL
VALIDATE_COMPONENT_FILENAME("foo_nsync.dll");

// Enable cfg_var downgrade
FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
