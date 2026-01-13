#pragma once

#include <SDK/foobar2000.h>
#include "config.h"

// Manages playlist sync polling and updates
// Manages playlist sync polling and updates
class sync_manager {
public:
    static sync_manager& get();
    
    // Lifecycle
    void start();
    void stop();
    
    // Update internal state from config
    void reload_config();
    
    void on_timer();
    
    // Callback interface
    class isync_callback {
    public:
        virtual void on_sync_progress(size_t job_index, const char* status, int percent) = 0;
        virtual void on_sync_complete(size_t job_index, const char* status) = 0;
    };
    
    void add_callback(isync_callback* cb);
    void remove_callback(isync_callback* cb);

    // Manual sync trigger
    void sync_now(size_t job_index);
    void sync_all();
    
    // Status
    bool is_syncing(size_t job_index) const;
    
private:
    sync_manager() = default;
    
    void start_timer();
    void stop_timer();
    
    void check_and_sync_job(size_t job_index);
    void update_playlist(const SyncJob& job, const pfc::string8& playlist_content);
    
    // Parse m3u8 content into file paths
    void parse_m3u8(const pfc::string8& content, pfc::list_t<pfc::string8>& out_paths);
    
    // Find or create playlist by name, returns index
    size_t find_or_create_playlist(const char* name);
    
    UINT_PTR m_timer_id = 0;
    std::vector<bool> m_syncing;
    int m_tick_count = 0;
    
    pfc::list_t<isync_callback*> m_callbacks;
};
