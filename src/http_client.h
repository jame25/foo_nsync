#pragma once

#include <pfc/pfc.h>
#include <functional>
#include <memory>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

// Async HTTP client using WinHTTP
class nsync_http_client {
public:
    using completion_callback = std::function<void(bool success, const pfc::string8& response, const pfc::string8& error)>;
    
    static nsync_http_client& get();
    
    // Async GET request - callback invoked on main thread
    void get_async(const char* url, completion_callback callback);
    
    // Sync GET for simple cases (blocks calling thread)
    bool get_sync(const char* url, pfc::string8& out_response, pfc::string8& out_error);

    // Sync GET for binary data (images, etc.)
    bool get_binary_sync(const char* url, pfc::array_t<uint8_t>& out_data, pfc::string8& out_error);
    
private:
    nsync_http_client();
    ~nsync_http_client();
    
    HINTERNET m_session = nullptr;
};

// Helper to parse URL components
struct url_parts {
    pfc::string8 scheme;
    pfc::string8 host;
    int port = 80;
    pfc::string8 path;
    
    static bool parse(const char* url, url_parts& out);
};
