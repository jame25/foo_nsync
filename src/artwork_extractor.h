#pragma once

#include <SDK/foobar2000.h>
#include <SDK/album_art.h>
#include <SDK/album_art_helpers.h>
#include "http_client.h"

// Helper function to check if a path is an nsync stream URL
bool is_nsync_stream_url(const char* path);

// Helper function to transform stream URL to artwork URL
pfc::string8 stream_url_to_artwork_url(const char* stream_url);

// Album art extractor instance for nsync HTTP streams
// Fetches artwork from the server's /artwork/ endpoint
class nsync_artwork_extractor_instance : public album_art_extractor_instance_v2 {
public:
    nsync_artwork_extractor_instance(const char* stream_url);

    // album_art_extractor_instance interface
    album_art_data_ptr query(const GUID& p_what, abort_callback& p_abort) override;

    // album_art_extractor_instance_v2 interface
    album_art_path_list::ptr query_paths(const GUID& p_what, abort_callback& p_abort) override;

private:
    pfc::string8 m_stream_url;
    pfc::string8 m_artwork_url;
    album_art_data_ptr m_cached_art;
    bool m_cache_checked = false;
};

// Album art extractor entrypoint for nsync HTTP streams
// Handles URLs matching the pattern http://...:.../stream/...
class nsync_artwork_extractor : public album_art_extractor {
public:
    // album_art_extractor interface
    bool is_our_path(const char* p_path, const char* p_extension) override;
    album_art_extractor_instance_ptr open(file_ptr p_filehint, const char* p_path, abort_callback& p_abort) override;
};

// Album art fallback for nsync HTTP streams
// Called when standard extractors don't find artwork
class nsync_artwork_fallback : public album_art_fallback {
public:
    album_art_extractor_instance_v2::ptr open(metadb_handle_list_cref items,
        pfc::list_base_const_t<GUID> const& ids, abort_callback& abort) override;
};
