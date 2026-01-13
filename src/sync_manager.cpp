#include "stdafx.h"
#include "sync_manager.h"
#include "http_client.h"
#include <SDK/playlist.h>

namespace {
    // Timer callback - Windows message-based timer
    void CALLBACK timer_proc(HWND, UINT, UINT_PTR, DWORD) {
        sync_manager::get().on_timer();
    }
}

sync_manager& sync_manager::get() {
    static sync_manager instance;
    return instance;
}

void sync_manager::start() {
    console::print("foo_nsync: Initializing...");
    
    auto& config = sync_config::get();
    m_syncing.resize(config.get_job_count(), false);
    
    if (config.is_enabled() && config.get_job_count() > 0) {
        start_timer();
        // Initial sync on startup
        sync_all();
    }
    
    console::formatter() << "foo_nsync: " << config.get_job_count() << " sync job(s) configured";
}

void sync_manager::stop() {
    console::print("foo_nsync: Shutting down...");
    stop_timer();
}

void sync_manager::reload_config() {
    auto& config = sync_config::get();
    // Ensure m_syncing matches job count (preserve existing states if possible, though unlikely needed)
    if (m_syncing.size() != config.get_job_count()) {
        m_syncing.resize(config.get_job_count(), false);
    }
}

void sync_manager::start_timer() {
    if (m_timer_id == 0) {
        // 1-second tick interval (jobs have individual poll intervals)
        m_timer_id = SetTimer(NULL, 0, 1000, timer_proc);
    }
}

void sync_manager::stop_timer() {
    if (m_timer_id != 0) {
        KillTimer(NULL, m_timer_id);
        m_timer_id = 0;
    }
}

void sync_manager::on_timer() {
    m_tick_count++;
    
    auto& config = sync_config::get();
    if (!config.is_enabled()) return;
    
    for (size_t i = 0; i < config.get_job_count(); ++i) {
        const auto& job = config.get_job(i);
        if (job.enabled && !m_syncing[i]) {
            // Check if it's time to poll this job
            if (m_tick_count % job.poll_interval_seconds == 0) {
                check_and_sync_job(i);
            }
        }
    }
}

void sync_manager::sync_now(size_t job_index) {
    auto& config = sync_config::get();
    
    // Safety check: ensure vector is sized
    if (m_syncing.size() < config.get_job_count()) {
        reload_config();
    }
    
    if (job_index < config.get_job_count() && job_index < m_syncing.size() && !m_syncing[job_index]) {
        check_and_sync_job(job_index);
    }
}

void sync_manager::sync_all() {
    auto& config = sync_config::get();
    for (size_t i = 0; i < config.get_job_count(); ++i) {
        const auto& job = config.get_job(i);
        if (job.enabled && !m_syncing[i]) {
            check_and_sync_job(i);
        }
    }
}

bool sync_manager::is_syncing(size_t job_index) const {
    return job_index < m_syncing.size() && m_syncing[job_index];
}

void sync_manager::add_callback(isync_callback* cb) {
    m_callbacks.add_item(cb);
}

void sync_manager::remove_callback(isync_callback* cb) {
    m_callbacks.remove_item(cb);
}

void sync_manager::check_and_sync_job(size_t job_index) {
    auto& config = sync_config::get();
    if (job_index >= config.get_job_count()) return;
    
    const auto& job = config.get_job(job_index);
    m_syncing[job_index] = true;
    
    // Notify start
    for (size_t i = 0; i < m_callbacks.get_count(); ++i) {
        m_callbacks[i]->on_sync_progress(job_index, "Checking...", 10);
    }
    
    // Build hash URL
    pfc::string8 hash_url;
    hash_url << job.server_url << "/hash/" << job.playlist_endpoint;
    
    nsync_http_client::get().get_async(hash_url.c_str(), 
        [this, job_index](bool success, const pfc::string8& response, const pfc::string8& error) {
            auto& config = sync_config::get();
            if (job_index >= config.get_job_count()) {
                m_syncing[job_index] = false;
                return;
            }
            
            auto& job = config.get_job_mutable(job_index);
            
            if (!success) {
                job.last_error = error;
                m_syncing[job_index] = false;
                console::formatter() << "foo_nsync: Error checking " << job.playlist_endpoint << ": " << error;
                // Notify error
                for (size_t i = 0; i < m_callbacks.get_count(); ++i) {
                    m_callbacks[i]->on_sync_complete(job_index, "Error");
                }
                return;
            }
            
            // Check if hash changed
            bool force_update = false;
            
            // Check if playlist actually exists
            size_t idx = playlist_manager::get()->find_playlist(job.target_playlist, pfc_infinite);
            if (idx == pfc_infinite) {
                force_update = true;
            }
            
            if (response == job.last_hash && !force_update) {
                // No change
                job.last_error.reset();
                m_syncing[job_index] = false;
                // Notify OK
                for (size_t i = 0; i < m_callbacks.get_count(); ++i) {
                    m_callbacks[i]->on_sync_complete(job_index, "OK (No Change)");
                }
                return;
            }
            
            // Hash changed - download playlist
            // Notify download
            for (size_t i = 0; i < m_callbacks.get_count(); ++i) {
                m_callbacks[i]->on_sync_progress(job_index, "Downloading...", 50);
            }
            
            pfc::string8 playlist_url;
            playlist_url << job.server_url << "/playlist/" << job.playlist_endpoint;
            
            nsync_http_client::get().get_async(playlist_url.c_str(),
                [this, job_index, new_hash = response](bool success, const pfc::string8& response, const pfc::string8& error) {
                    auto& config = sync_config::get();
                    if (job_index >= config.get_job_count()) {
                        m_syncing[job_index] = false;
                        return;
                    }
                    
                    auto& job = config.get_job_mutable(job_index);
                    
                    if (!success) {
                        job.last_error = error;
                        m_syncing[job_index] = false;
                        console::formatter() << "foo_nsync: Error downloading " << job.playlist_endpoint << ": " << error;
                        // Notify error
                        for (size_t i = 0; i < m_callbacks.get_count(); ++i) {
                           m_callbacks[i]->on_sync_complete(job_index, "Error");
                        }
                        return;
                    }
                    
                    // Notify processing
                    for (size_t i = 0; i < m_callbacks.get_count(); ++i) {
                        m_callbacks[i]->on_sync_progress(job_index, "Updating Playlist...", 80);
                    }
                    
                    // Update playlist
                    update_playlist(job, response);
                    
                    // Update stored hash
                    job.last_hash = new_hash;
                    job.last_error.reset();
                    config.save();
                    
                    m_syncing[job_index] = false;
                    console::formatter() << "foo_nsync: Updated playlist '" << job.target_playlist << "'";
                    
                    // Notify completion
                    for (size_t i = 0; i < m_callbacks.get_count(); ++i) {
                        m_callbacks[i]->on_sync_complete(job_index, "OK");
                    }
                });
        });
}

void sync_manager::parse_m3u8(const pfc::string8& content, pfc::list_t<pfc::string8>& out_paths) {
    // Split by lines and extract file paths
    const char* ptr = content.c_str();
    const char* end = ptr + content.length();
    
    while (ptr < end) {
        const char* line_end = ptr;
        while (line_end < end && *line_end != '\n' && *line_end != '\r') {
            ++line_end;
        }
        
        // Skip empty lines and comments
        if (line_end > ptr && *ptr != '#') {
            pfc::string8 line;
            line.set_string(ptr, line_end - ptr);
            
            // Trim whitespace
            line.skip_trailing_char(' ');
            line.skip_trailing_char('\t');
            
            if (line.length() > 0) {
                // Check if it's a relative path/URL from our generator
                if (line.has_prefix("/stream/") || line.has_prefix("/")) {
                    // Prepend server URL
                    // Need to access current job config here. 
                    // But parse_m3u8 is a helper that doesn't know the job.
                    // We should move this logic to update_playlist or pass the prefix.
                    // For now, let's just add it as is, and handle it in update_playlist.
                }
                out_paths.add_item(line);
            }
        }
        
        // Move past line ending
        ptr = line_end;
        while (ptr < end && (*ptr == '\n' || *ptr == '\r')) {
            ++ptr;
        }
    }
}

size_t sync_manager::find_or_create_playlist(const char* name) {
    auto api = playlist_manager::get();
    
    // Try to find existing playlist
    size_t index = api->find_playlist(name, pfc_infinite);
    if (index != pfc_infinite) {
        return index;
    }
    
    // Create new playlist
    return api->create_playlist(name, pfc_infinite, pfc_infinite);
}

void sync_manager::update_playlist(const SyncJob& job, const pfc::string8& playlist_content) {
    // Parse m3u8 content
    pfc::list_t<pfc::string8> file_paths;
    parse_m3u8(playlist_content, file_paths);
    
    // Apply path mappings
    for (size_t i = 0; i < file_paths.get_count(); ++i) {
        pfc::string8& path = file_paths[i];
        
        // Check for streaming URL (new mode)
        if (path.has_prefix("/stream/")) {
            pfc::string8 full_url = job.server_url;
            full_url << path;
            path = full_url;
            continue;
        }



        // Apply manual mapping (legacy/local mode)
        // [Removed: User prefers streaming mode without config]
    }
    
    if (file_paths.get_count() == 0) {
        console::formatter() << "foo_nsync: Warning - playlist '" << job.target_playlist << "' is empty";
        return;
    }
    
    // Find or create target playlist
    size_t playlist_index = find_or_create_playlist(job.target_playlist.c_str());
    
    auto api = playlist_manager::get();
    
    // Clear existing items
    api->playlist_clear(playlist_index);
    
    // Convert to location list for adding
    pfc::list_t<const char*> locations;
    for (size_t i = 0; i < file_paths.get_count(); ++i) {
        locations.add_item(file_paths[i].c_str());
    }
    
    // Use standard playlist API to add items
    // This handles file resolution and filtering automatically
    
    // Use simpler AddLocations approach via playlist API
    api->playlist_add_locations(playlist_index, locations, false, nullptr);
    
    console::formatter() << "foo_nsync: Added " << file_paths.get_count() << " items to '" << job.target_playlist << "'";
}

// Initquit service to manage sync_manager lifecycle
class sync_initquit : public initquit {
public:
    void on_init() override {
        sync_manager::get().start();
    }
    void on_quit() override {
        sync_manager::get().stop();
    }
};

static initquit_factory_t<sync_initquit> g_sync_initquit_factory;
