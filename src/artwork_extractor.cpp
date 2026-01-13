#include "stdafx.h"
#include "artwork_extractor.h"
#include "guids.h"
#include <set>
#include <mutex>

// Simple cache to avoid repeated failed requests
static std::set<pfc::string8> g_failed_urls;
static std::mutex g_failed_urls_mutex;

static bool is_url_failed(const char* url) {
    std::lock_guard<std::mutex> lock(g_failed_urls_mutex);
    return g_failed_urls.find(pfc::string8(url)) != g_failed_urls.end();
}

static void mark_url_failed(const char* url) {
    std::lock_guard<std::mutex> lock(g_failed_urls_mutex);
    // Limit cache size to prevent memory bloat
    if (g_failed_urls.size() > 1000) {
        g_failed_urls.clear();
    }
    g_failed_urls.insert(pfc::string8(url));
}

// Log when the component initializes to verify it's loaded
class nsync_artwork_initquit : public initquit {
public:
    void on_init() override {
        console::formatter() << "foo_nsync [art]: Album art extractor and fallback services REGISTERED";
    }
    void on_quit() override {
        std::lock_guard<std::mutex> lock(g_failed_urls_mutex);
        g_failed_urls.clear();
    }
};
static initquit_factory_t<nsync_artwork_initquit> g_artwork_initquit;

// Helper function implementations

bool is_nsync_stream_url(const char* path) {
    if (path == nullptr) return false;

    // Must start with http:// or https://
    bool is_http = (strncmp(path, "http://", 7) == 0);
    bool is_https = (strncmp(path, "https://", 8) == 0);

    if (!is_http && !is_https) return false;

    // Must contain /stream/ marker
    if (strstr(path, "/stream/") == nullptr) return false;

    return true;
}

pfc::string8 stream_url_to_artwork_url(const char* stream_url) {
    pfc::string8 artwork_url;

    const char* stream_marker = strstr(stream_url, "/stream/");
    if (stream_marker) {
        size_t prefix_len = stream_marker - stream_url;
        artwork_url.set_string(stream_url, prefix_len);
        artwork_url << "/artwork/" << (stream_marker + 8); // +8 skips "/stream/"
    } else {
        artwork_url = stream_url;
    }

    return artwork_url;
}

// nsync_artwork_extractor_instance implementation

nsync_artwork_extractor_instance::nsync_artwork_extractor_instance(const char* stream_url)
    : m_stream_url(stream_url)
{
    m_artwork_url = stream_url_to_artwork_url(stream_url);
    console::formatter() << "foo_nsync [art]: Instance created for: " << m_stream_url;
    console::formatter() << "foo_nsync [art]: Artwork URL: " << m_artwork_url;
}

album_art_data_ptr nsync_artwork_extractor_instance::query(const GUID& p_what, abort_callback& p_abort) {
    p_abort.check();

    // Only handle front cover for now
    if (p_what != album_art_ids::cover_front) {
        throw exception_album_art_not_found();
    }

    // Return cached art if available
    if (m_cache_checked) {
        if (m_cached_art.is_valid()) {
            return m_cached_art;
        }
        throw exception_album_art_not_found();
    }

    m_cache_checked = true;

    // Check if this URL previously failed (avoid repeated timeouts)
    if (is_url_failed(m_artwork_url.c_str())) {
        console::formatter() << "foo_nsync [art]: Skipping previously failed URL: " << m_artwork_url;
        throw exception_album_art_not_found();
    }

    // Fetch artwork from server
    console::formatter() << "foo_nsync [art]: Fetching artwork: " << m_artwork_url;
    pfc::array_t<uint8_t> image_data;
    pfc::string8 error;

    if (!nsync_http_client::get().get_binary_sync(m_artwork_url.c_str(), image_data, error)) {
        console::formatter() << "foo_nsync [art]: FAILED: " << error;
        mark_url_failed(m_artwork_url.c_str());
        throw exception_album_art_not_found();
    }

    if (image_data.get_size() == 0) {
        mark_url_failed(m_artwork_url.c_str());
        throw exception_album_art_not_found();
    }

    // Create album art data from the fetched image
    m_cached_art = album_art_data_impl::g_create(image_data.get_ptr(), image_data.get_size());

    console::formatter() << "foo_nsync [art]: SUCCESS - " << (int)image_data.get_size() << " bytes";

    return m_cached_art;
}

album_art_path_list::ptr nsync_artwork_extractor_instance::query_paths(const GUID& p_what, abort_callback& p_abort) {
    p_abort.check();
    // HTTP URLs cannot be opened as file paths by components like Columns UI's WIC decoder.
    // Return an empty path list to indicate "artwork exists but has no filesystem path".
    // This forces consumers to use query() to get the actual image data.
    if (p_what == album_art_ids::cover_front) {
        return new service_impl_t<album_art_path_list_dummy>();
    }
    throw exception_album_art_not_found();
}

// nsync_artwork_extractor implementation

bool nsync_artwork_extractor::is_our_path(const char* p_path, const char* p_extension) {
    (void)p_extension;

    bool result = is_nsync_stream_url(p_path);
    console::formatter() << "foo_nsync [art]: is_our_path('" << (p_path ? p_path : "null") << "') = " << (result ? "YES" : "NO");
    return result;
}

album_art_extractor_instance_ptr nsync_artwork_extractor::open(file_ptr p_filehint, const char* p_path, abort_callback& p_abort) {
    (void)p_filehint;
    p_abort.check();

    console::formatter() << "foo_nsync [art]: extractor open() called for: " << p_path;

    if (!is_nsync_stream_url(p_path)) {
        console::formatter() << "foo_nsync [art]: open() - not our path, throwing unsupported";
        throw exception_album_art_unsupported_format();
    }

    console::formatter() << "foo_nsync [art]: open() - creating extractor instance";
    return new service_impl_t<nsync_artwork_extractor_instance>(p_path);
}

// nsync_artwork_fallback implementation

album_art_extractor_instance_v2::ptr nsync_artwork_fallback::open(
    metadb_handle_list_cref items,
    pfc::list_base_const_t<GUID> const& ids,
    abort_callback& abort)
{
    console::formatter() << "foo_nsync [art]: FALLBACK open() called with " << items.get_count() << " items";

    // Look for an nsync stream URL in the items
    for (size_t i = 0; i < items.get_count(); ++i) {
        abort.check();

        const char* path = items[i]->get_path();
        console::formatter() << "foo_nsync [art]: FALLBACK checking item " << i << ": " << path;

        if (is_nsync_stream_url(path)) {
            console::formatter() << "foo_nsync [art]: FALLBACK found nsync URL, creating instance";
            return new service_impl_t<nsync_artwork_extractor_instance>(path);
        }
    }

    console::formatter() << "foo_nsync [art]: FALLBACK no nsync URLs found";
    throw exception_album_art_not_found();
}

// Register services
static service_factory_single_t<nsync_artwork_extractor> g_nsync_artwork_extractor_factory;
static service_factory_single_t<nsync_artwork_fallback> g_nsync_artwork_fallback_factory;
