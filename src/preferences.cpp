#include "stdafx.h"
#include "preferences.h"
#include "guids.h"
#include "sync_manager.h"

// Edit job dialog implementation
BOOL CEditJobDialog::OnInitDialog(CWindow, LPARAM) {
    m_dark.AddDialogWithControls(*this);
    
    SetDlgItemText(IDC_SERVER_URL, pfc::stringcvt::string_os_from_utf8(m_job.server_url.c_str()));
    SetDlgItemText(IDC_ENDPOINT, pfc::stringcvt::string_os_from_utf8(m_job.playlist_endpoint.c_str()));
    SetDlgItemText(IDC_TARGET_PLAYLIST, pfc::stringcvt::string_os_from_utf8(m_job.target_playlist.c_str()));
    SetDlgItemInt(IDC_POLL_INTERVAL, m_job.poll_interval_seconds, FALSE);
    CheckDlgButton(IDC_JOB_ENABLED, m_job.enabled ? BST_CHECKED : BST_UNCHECKED);
    
    return TRUE;
}

void CEditJobDialog::OnOK(UINT, int, CWindow) {
    // Read values back
    CString temp;
    
    GetDlgItemText(IDC_SERVER_URL, temp);
    m_job.server_url = pfc::stringcvt::string_utf8_from_os(temp);
    
    GetDlgItemText(IDC_ENDPOINT, temp);
    m_job.playlist_endpoint = pfc::stringcvt::string_utf8_from_os(temp);
    
    GetDlgItemText(IDC_TARGET_PLAYLIST, temp);
    m_job.target_playlist = pfc::stringcvt::string_utf8_from_os(temp);
    
    m_job.poll_interval_seconds = GetDlgItemInt(IDC_POLL_INTERVAL, NULL, FALSE);
    if (m_job.poll_interval_seconds < 10) m_job.poll_interval_seconds = 10;  // Minimum 10 seconds
    
    m_job.enabled = IsDlgButtonChecked(IDC_JOB_ENABLED) == BST_CHECKED;
    
    EndDialog(IDOK);
}

void CEditJobDialog::OnCancel(UINT, int, CWindow) {
    EndDialog(IDCANCEL);
}

// Main preferences page implementation
BOOL CPreferencesPage::OnInitDialog(CWindow, LPARAM) {
    m_dark.AddDialogWithControls(*this);
    
    // Initialize list control
    m_list = GetDlgItem(IDC_JOB_LIST);
    m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);
    
    // Add columns
    m_list.InsertColumn(0, L"Server URL", LVCFMT_LEFT, 150);
    m_list.InsertColumn(1, L"Endpoint", LVCFMT_LEFT, 80);
    m_list.InsertColumn(2, L"Target Playlist", LVCFMT_LEFT, 100);
    m_list.InsertColumn(3, L"Interval", LVCFMT_RIGHT, 50);
    m_list.InsertColumn(4, L"Status", LVCFMT_LEFT, 60);
    
    // Load current config
    auto& config = sync_config::get();
    m_enabled = config.is_enabled();
    m_jobs.clear();
    for (size_t i = 0; i < config.get_job_count(); ++i) {
        m_jobs.push_back(config.get_job(i));
    }
    
    CheckDlgButton(IDC_ENABLED, m_enabled ? BST_CHECKED : BST_UNCHECKED);
    PopulateList();
    UpdateButtons();
    
    // Register callback
    sync_manager::get().add_callback(this);
    
    return TRUE;
}

void CPreferencesPage::OnDestroy() {
    // Unregister callback
    sync_manager::get().remove_callback(this);
}

void CPreferencesPage::on_sync_progress(size_t job_index, const char* status, int percent) {
    // Update list status column
    if (job_index < m_jobs.size()) {
        pfc::stringcvt::string_wide_from_utf8 wide_status(status);
        m_list.SetItemText(job_index, 4, wide_status);
    }
    
    // Update active status label if selected
    int sel = m_list.GetSelectedIndex();
    if (sel >= 0 && (size_t)sel == job_index) {
        pfc::string8 msg;
        msg << status << " (" << percent << "%)";
        pfc::stringcvt::string_wide_from_utf8 wide_msg(msg);
        SetDlgItemText(IDC_STATUS, wide_msg);
    }
}

void CPreferencesPage::on_sync_complete(size_t job_index, const char* status) {
    // Update list status column
    if (job_index < m_jobs.size()) {
        pfc::stringcvt::string_wide_from_utf8 wide_status(status);
        m_list.SetItemText(job_index, 4, wide_status);
    }
    
    // Update active status label if selected
    int sel = m_list.GetSelectedIndex();
    if (sel >= 0 && (size_t)sel == job_index) {
        pfc::stringcvt::string_wide_from_utf8 wide_msg(status);
        SetDlgItemText(IDC_STATUS, wide_msg);
    }
}

void CPreferencesPage::PopulateList() {
    m_list.DeleteAllItems();
    
    for (size_t i = 0; i < m_jobs.size(); ++i) {
        const auto& job = m_jobs[i];
        
        int idx = m_list.InsertItem(i, pfc::stringcvt::string_os_from_utf8(job.server_url.c_str()));
        m_list.SetItemText(idx, 1, pfc::stringcvt::string_os_from_utf8(job.playlist_endpoint.c_str()));
        m_list.SetItemText(idx, 2, pfc::stringcvt::string_os_from_utf8(job.target_playlist.c_str()));
        
        wchar_t interval[32];
        swprintf_s(interval, L"%d", job.poll_interval_seconds);
        m_list.SetItemText(idx, 3, interval);
        
        const wchar_t* status = job.last_error.is_empty() ? L"OK" : L"Error";
        m_list.SetItemText(idx, 4, status);
        
        m_list.SetCheckState(idx, job.enabled);
    }
}

void CPreferencesPage::UpdateButtons() {
    int sel = m_list.GetSelectedIndex();
    bool has_selection = sel >= 0;
    
    GetDlgItem(IDC_EDIT).EnableWindow(has_selection);
    GetDlgItem(IDC_REMOVE).EnableWindow(has_selection);
    GetDlgItem(IDC_SYNC_NOW).EnableWindow(has_selection && m_jobs[sel].enabled);
}

void CPreferencesPage::OnEnabledChanged(UINT, int, CWindow) {
    m_enabled = IsDlgButtonChecked(IDC_ENABLED) == BST_CHECKED;
    OnChanged();
}

void CPreferencesPage::OnAdd(UINT, int, CWindow) {
    SyncJob new_job;
    new_job.server_url = "http://localhost:8090";
    new_job.playlist_endpoint = "music";
    new_job.target_playlist = "Synced Music";
    new_job.poll_interval_seconds = 60;
    new_job.enabled = true;
    
    CEditJobDialog dlg(new_job);
    if (dlg.DoModal(*this) == IDOK) {
        m_jobs.push_back(new_job);
        PopulateList();
        OnChanged();
    }
}

void CPreferencesPage::OnEdit(UINT, int, CWindow) {
    int sel = m_list.GetSelectedIndex();
    if (sel < 0 || sel >= (int)m_jobs.size()) return;
    
    CEditJobDialog dlg(m_jobs[sel]);
    if (dlg.DoModal(*this) == IDOK) {
        PopulateList();
        OnChanged();
    }
}

void CPreferencesPage::OnRemove(UINT, int, CWindow) {
    int sel = m_list.GetSelectedIndex();
    if (sel < 0 || sel >= (int)m_jobs.size()) return;
    
    m_jobs.erase(m_jobs.begin() + sel);
    PopulateList();
    UpdateButtons();
    OnChanged();
}

void CPreferencesPage::OnSyncNow(UINT, int, CWindow) {
    int sel = m_list.GetSelectedIndex();
    if (sel < 0 || sel >= (int)m_jobs.size()) return;
    
    // Apply changes first
    apply();
    
    // Trigger sync
    sync_manager::get().sync_now(sel);
    
    SetDlgItemText(IDC_STATUS, L"Syncing...");
}

LRESULT CPreferencesPage::OnListSelectionChanged(LPNMHDR) {
    UpdateButtons();
    return 0;
}

LRESULT CPreferencesPage::OnListDoubleClick(LPNMHDR) {
    OnEdit(0, 0, CWindow());
    return 0;
}

t_uint32 CPreferencesPage::get_state() {
    t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
    if (HasChanged()) state |= preferences_state::changed;
    return state;
}

void CPreferencesPage::apply() {
    auto& config = sync_config::get();
    
    config.set_enabled(m_enabled);
    
    // Clear and re-add all jobs
    while (config.get_job_count() > 0) {
        config.remove_job(0);
    }
    for (const auto& job : m_jobs) {
        config.add_job(job);
    }
    
    // Notify manager of changes
    sync_manager::get().reload_config();
    
    OnChanged();
}

void CPreferencesPage::reset() {
    m_enabled = true;
    m_jobs.clear();
    
    CheckDlgButton(IDC_ENABLED, BST_CHECKED);
    PopulateList();
    UpdateButtons();
    OnChanged();
}

bool CPreferencesPage::HasChanged() {
    auto& config = sync_config::get();
    
    if (m_enabled != config.is_enabled()) return true;
    if (m_jobs.size() != config.get_job_count()) return true;
    
    for (size_t i = 0; i < m_jobs.size(); ++i) {
        const auto& a = m_jobs[i];
        const auto& b = config.get_job(i);
        if (a.server_url != b.server_url) return true;
        if (a.playlist_endpoint != b.playlist_endpoint) return true;
        if (a.target_playlist != b.target_playlist) return true;
        if (a.poll_interval_seconds != b.poll_interval_seconds) return true;
        if (a.enabled != b.enabled) return true;
    }
    
    return false;
}

void CPreferencesPage::OnChanged() {
    m_callback->on_state_changed();
}

// Factory
GUID nsync_preferences_page_impl::get_guid() {
    return guid_preferences_page;
}

// Factory registration
static service_factory_single_t<nsync_preferences_page_impl> g_preferences_page_factory;
