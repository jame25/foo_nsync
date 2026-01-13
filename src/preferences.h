#pragma once

#include <SDK/foobar2000.h>
#include <helpers/foobar2000+atl.h>
#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>
#include <libPPUI/CListControlComplete.h>
#include "resource.h"
#include "config.h"
#include "sync_manager.h"

// Edit job dialog
class CEditJobDialog : public CDialogImpl<CEditJobDialog> {
public:
    enum { IDD = IDD_EDIT_JOB };
    
    CEditJobDialog(SyncJob& job) : m_job(job) {}
    
    BEGIN_MSG_MAP_EX(CEditJobDialog)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER_EX(IDOK, OnOK)
        COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
    END_MSG_MAP()
    
private:
    BOOL OnInitDialog(CWindow, LPARAM);
    void OnOK(UINT, int, CWindow);
    void OnCancel(UINT, int, CWindow);
    
    SyncJob& m_job;
    fb2k::CDarkModeHooks m_dark;
};

// Main preferences page
class CPreferencesPage : public CDialogImpl<CPreferencesPage>, public preferences_page_instance, public sync_manager::isync_callback {
public:
    enum { IDD = IDD_PREFERENCES };
    
    CPreferencesPage(preferences_page_callback::ptr callback) : m_callback(callback) {}
    
    t_uint32 get_state() override;
    void apply() override;
    void reset() override;
    
    // Sync callbacks
    void on_sync_progress(size_t job_index, const char* status, int percent) override;
    void on_sync_complete(size_t job_index, const char* status) override;
    
    BEGIN_MSG_MAP_EX(CPreferencesPage)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_DESTROY(OnDestroy)
        COMMAND_HANDLER_EX(IDC_ENABLED, BN_CLICKED, OnEnabledChanged)
        COMMAND_HANDLER_EX(IDC_ENABLED, BN_CLICKED, OnEnabledChanged)
        COMMAND_HANDLER_EX(IDC_ADD, BN_CLICKED, OnAdd)
        COMMAND_HANDLER_EX(IDC_EDIT, BN_CLICKED, OnEdit)
        COMMAND_HANDLER_EX(IDC_REMOVE, BN_CLICKED, OnRemove)
        COMMAND_HANDLER_EX(IDC_SYNC_NOW, BN_CLICKED, OnSyncNow)
        NOTIFY_HANDLER_EX(IDC_JOB_LIST, LVN_ITEMCHANGED, OnListSelectionChanged)
        NOTIFY_HANDLER_EX(IDC_JOB_LIST, NM_DBLCLK, OnListDoubleClick)
    END_MSG_MAP()
    
private:
    BOOL OnInitDialog(CWindow, LPARAM);
    void OnDestroy();
    void OnEnabledChanged(UINT, int, CWindow);
    void OnAdd(UINT, int, CWindow);
    void OnEdit(UINT, int, CWindow);
    void OnRemove(UINT, int, CWindow);
    void OnSyncNow(UINT, int, CWindow);
    LRESULT OnListSelectionChanged(LPNMHDR);
    LRESULT OnListDoubleClick(LPNMHDR);
    
    void PopulateList();
    void UpdateButtons();
    void OnChanged();
    bool HasChanged();
    
    preferences_page_callback::ptr m_callback;
    CListViewCtrl m_list;
    fb2k::CDarkModeHooks m_dark;
    
    // Local copy for editing
    std::vector<SyncJob> m_jobs;
    bool m_enabled = true;
};

// Preferences page factory
// Preferences page factory
class nsync_preferences_page_impl : public preferences_page_impl<CPreferencesPage> {
public:
    const char* get_name() override { return "Playlist Sync"; }
    GUID get_guid() override;
    GUID get_parent_guid() override { return guid_tools; }
};
