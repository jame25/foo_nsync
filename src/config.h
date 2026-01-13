#pragma once

#include <SDK/foobar2000.h>
#include <pfc/pfc.h>
#include <string>
#include <vector>

// Represents a single playlist sync job
struct SyncJob {
    pfc::string8 server_url;        // e.g., "http://192.168.1.10:8090"
    pfc::string8 playlist_endpoint; // e.g., "music" -> /playlist/music
    pfc::string8 target_playlist;   // foobar2000 playlist name
    bool enabled = true;
    int poll_interval_seconds = 60;
    pfc::string8 last_hash;         // Last known MD5 from server
    pfc::string8 last_error;        // Last error message (if any)
    
    // For serialization
    template<typename t_stream>
    void write(t_stream& p_stream, abort_callback& p_abort) const {
        p_stream << server_url;
        p_stream << playlist_endpoint;
        p_stream << target_playlist;
        p_stream << enabled;
        p_stream << poll_interval_seconds;
        p_stream << last_hash;
    }

    template<typename t_stream>
    void read(t_stream& p_stream, abort_callback& p_abort) {
        p_stream >> server_url;
        p_stream >> playlist_endpoint;
        p_stream >> target_playlist;
        p_stream >> enabled;
        p_stream >> poll_interval_seconds;
        p_stream >> last_hash;
    }
    
    // Stream operators for cfg_objList
    template<typename t_stream>
    friend t_stream& operator<<(t_stream& p_stream, const SyncJob& p_item) {
        p_item.write(p_stream, abort_callback_dummy());
        return p_stream;
    }
    
    template<typename t_stream>
    friend t_stream& operator>>(t_stream& p_stream, SyncJob& p_item) {
        p_item.read(p_stream, abort_callback_dummy());
        return p_stream;
    }
};

// Configuration manager
class sync_config {
public:
    static sync_config& get();
    
    // Job management
    size_t get_job_count() const { return m_jobs.size(); }
    const SyncJob& get_job(size_t index) const { return m_jobs[index]; }
    SyncJob& get_job_mutable(size_t index) { return m_jobs[index]; }
    
    void add_job(const SyncJob& job);
    void remove_job(size_t index);
    void update_job(size_t index, const SyncJob& job);
    
    // Global settings
    bool is_enabled() const { return m_enabled; }
    void set_enabled(bool enabled) { m_enabled = enabled; }
    
    int get_default_interval() const { return m_default_interval; }
    void set_default_interval(int seconds) { m_default_interval = seconds; }
    
    // Persistence
    void save();
    void load();
    
private:
    sync_config();
    
    std::vector<SyncJob> m_jobs;
    bool m_enabled = true;
    int m_default_interval = 60;
};
