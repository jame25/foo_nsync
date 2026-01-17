#include "stdafx.h"
#include "http_client.h"
#include <thread>

nsync_http_client& nsync_http_client::get() {
    static nsync_http_client instance;
    return instance;
}

nsync_http_client::nsync_http_client() {
    m_session = WinHttpOpen(
        L"foo_nsync/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
}

nsync_http_client::~nsync_http_client() {
    if (m_session) {
        WinHttpCloseHandle(m_session);
    }
}

bool url_parts::parse(const char* url, url_parts& out) {
    pfc::string8 url_str(url);
    
    // Parse scheme
    const char* scheme_end = strstr(url, "://");
    if (!scheme_end) return false;
    
    out.scheme.set_string(url, scheme_end - url);
    const char* host_start = scheme_end + 3;
    
    // Parse host and port
    const char* path_start = strchr(host_start, '/');
    const char* port_start = strchr(host_start, ':');
    
    if (port_start && (!path_start || port_start < path_start)) {
        out.host.set_string(host_start, port_start - host_start);
        if (path_start) {
            pfc::string8 port_str;
            port_str.set_string(port_start + 1, path_start - port_start - 1);
            out.port = atoi(port_str.c_str());
        } else {
            out.port = atoi(port_start + 1);
        }
    } else if (path_start) {
        out.host.set_string(host_start, path_start - host_start);
        out.port = (out.scheme == "https") ? 443 : 80;
    } else {
        out.host = host_start;
        out.port = (out.scheme == "https") ? 443 : 80;
    }
    
    // Parse path
    out.path = path_start ? path_start : "/";
    
    return true;
}

bool nsync_http_client::get_sync(const char* url, pfc::string8& out_response, pfc::string8& out_error) {
    if (!m_session) {
        out_error = "HTTP session not initialized";
        return false;
    }

    url_parts parts;
    if (!url_parts::parse(url, parts)) {
        out_error = "Invalid URL";
        return false;
    }

    // Convert host to wide string
    pfc::stringcvt::string_wide_from_utf8 wide_host(parts.host.c_str());
    pfc::stringcvt::string_wide_from_utf8 wide_path(parts.path.c_str());

    HINTERNET hConnect = WinHttpConnect(
        m_session,
        wide_host.get_ptr(),
        parts.port,
        0
    );

    if (!hConnect) {
        DWORD err = GetLastError();
        out_error.reset();
        out_error << "Connection failed (error " << (int)err << ")";
        return false;
    }

    DWORD flags = (parts.scheme == "https") ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        wide_path.get_ptr(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );

    if (!hRequest) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hConnect);
        out_error.reset();
        out_error << "Request creation failed (error " << (int)err << ")";
        return false;
    }

    // Set timeouts (5 seconds)
    DWORD timeout = 5000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    BOOL bResults = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0
    );

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }

    if (!bResults) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        out_error.reset();
        out_error << "Request failed (error " << (int)err << ")";
        return false;
    }

    // Check status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX
    );

    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        out_error.reset();
        out_error << "HTTP " << (int)statusCode;
        return false;
    }
    
    // Read response
    out_response.reset();
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;
        
        pfc::array_t<char> buffer;
        buffer.set_size(dwSize + 1);
        
        if (WinHttpReadData(hRequest, buffer.get_ptr(), dwSize, &dwDownloaded)) {
            buffer[dwDownloaded] = '\0';
            out_response += buffer.get_ptr();
        }
    } while (dwSize > 0);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);

    return true;
}

bool nsync_http_client::get_binary_sync(const char* url, pfc::array_t<uint8_t>& out_data, pfc::string8& out_error) {
    if (!m_session) {
        out_error = "HTTP session not initialized";
        return false;
    }

    url_parts parts;
    if (!url_parts::parse(url, parts)) {
        out_error = "Invalid URL";
        return false;
    }

    // Convert host to wide string
    pfc::stringcvt::string_wide_from_utf8 wide_host(parts.host.c_str());
    pfc::stringcvt::string_wide_from_utf8 wide_path(parts.path.c_str());

    HINTERNET hConnect = WinHttpConnect(
        m_session,
        wide_host.get_ptr(),
        parts.port,
        0
    );

    if (!hConnect) {
        DWORD err = GetLastError();
        out_error.reset();
        out_error << "Connection failed (error " << (int)err << ")";
        return false;
    }

    DWORD flags = (parts.scheme == "https") ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        wide_path.get_ptr(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );

    if (!hRequest) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hConnect);
        out_error.reset();
        out_error << "Request creation failed (error " << (int)err << ")";
        return false;
    }

    // Set timeouts (2 seconds for artwork - must be fast to not block UI)
    DWORD timeout = 2000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    BOOL bResults = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0
    );

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }

    if (!bResults) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        out_error.reset();
        out_error << "Request failed (error " << (int)err << ")";
        return false;
    }

    // Check status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX
    );

    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        out_error.reset();
        out_error << "HTTP " << (int)statusCode;
        return false;
    }

    // Read binary response
    out_data.set_size(0);
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;

    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;

        size_t current_size = out_data.get_size();
        out_data.set_size(current_size + dwSize);

        if (WinHttpReadData(hRequest, out_data.get_ptr() + current_size, dwSize, &dwDownloaded)) {
            // Adjust size if we read less than expected
            if (dwDownloaded < dwSize) {
                out_data.set_size(current_size + dwDownloaded);
            }
        } else {
            out_data.set_size(current_size);
            break;
        }
    } while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);

    return out_data.get_size() > 0;
}

void nsync_http_client::get_async(const char* url, completion_callback callback) {
    pfc::string8 url_copy(url);

    std::thread([url_copy, callback]() {
        pfc::string8 response, error;
        bool success = nsync_http_client::get().get_sync(url_copy, response, error);

        // Invoke callback on main thread
        fb2k::inMainThread([callback, success, response, error]() {
            callback(success, response, error);
        });
    }).detach();
}

bool nsync_http_client::post_sync(const char* url, pfc::string8& out_response, pfc::string8& out_error) {
    if (!m_session) {
        out_error = "HTTP session not initialized";
        return false;
    }

    url_parts parts;
    if (!url_parts::parse(url, parts)) {
        out_error = "Invalid URL";
        return false;
    }

    pfc::stringcvt::string_wide_from_utf8 wide_host(parts.host.c_str());
    pfc::stringcvt::string_wide_from_utf8 wide_path(parts.path.c_str());

    HINTERNET hConnect = WinHttpConnect(
        m_session,
        wide_host.get_ptr(),
        parts.port,
        0
    );

    if (!hConnect) {
        DWORD err = GetLastError();
        out_error.reset();
        out_error << "Connection failed (error " << (int)err << ")";
        return false;
    }

    DWORD flags = (parts.scheme == "https") ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"POST",
        wide_path.get_ptr(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );

    if (!hRequest) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hConnect);
        out_error.reset();
        out_error << "Request creation failed (error " << (int)err << ")";
        return false;
    }

    // Set timeouts (10 seconds for sync operations - may need to scan directories)
    DWORD timeout = 10000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    BOOL bResults = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0
    );

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }

    if (!bResults) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        out_error.reset();
        out_error << "Request failed (error " << (int)err << ")";
        return false;
    }

    // Check status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX
    );

    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        out_error.reset();
        out_error << "HTTP " << (int)statusCode;
        return false;
    }

    // Read response
    out_response.reset();
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;

    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;

        pfc::array_t<char> buffer;
        buffer.set_size(dwSize + 1);

        if (WinHttpReadData(hRequest, buffer.get_ptr(), dwSize, &dwDownloaded)) {
            buffer[dwDownloaded] = '\0';
            out_response += buffer.get_ptr();
        }
    } while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);

    return true;
}

void nsync_http_client::post_async(const char* url, completion_callback callback) {
    pfc::string8 url_copy(url);

    std::thread([url_copy, callback]() {
        pfc::string8 response, error;
        bool success = nsync_http_client::get().post_sync(url_copy, response, error);

        // Invoke callback on main thread
        fb2k::inMainThread([callback, success, response, error]() {
            callback(success, response, error);
        });
    }).detach();
}
