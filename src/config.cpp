#include "stdafx.h"
#include "config.h"
#include "guids.h"
#include <SDK/cfg_var.h>

// cfg_var for persistence
static cfg_bool cfg_enabled(guid_cfg_enabled, true);
static cfg_int cfg_poll_interval(guid_cfg_poll_interval, 60);

// Binary blob for sync jobs
static cfg_objList<SyncJob> cfg_sync_jobs(guid_cfg_sync_jobs);

// SyncJob serialization is now handled by templates in config.h

// Singleton
sync_config& sync_config::get() {
    static sync_config instance;
    return instance;
}

sync_config::sync_config() {
    load();
}

void sync_config::add_job(const SyncJob& job) {
    m_jobs.push_back(job);
    save();
}

void sync_config::remove_job(size_t index) {
    if (index < m_jobs.size()) {
        m_jobs.erase(m_jobs.begin() + index);
        save();
    }
}

void sync_config::update_job(size_t index, const SyncJob& job) {
    if (index < m_jobs.size()) {
        m_jobs[index] = job;
        save();
    }
}

void sync_config::save() {
    cfg_enabled = m_enabled;
    cfg_poll_interval = m_default_interval;
    
    cfg_sync_jobs.remove_all();
    for (const auto& job : m_jobs) {
        cfg_sync_jobs.add_item(job);
    }
}

void sync_config::load() {
    m_enabled = cfg_enabled;
    m_default_interval = cfg_poll_interval;
    
    m_jobs.clear();
    for (size_t i = 0; i < cfg_sync_jobs.get_count(); ++i) {
        m_jobs.push_back(cfg_sync_jobs[i]);
    }
}
