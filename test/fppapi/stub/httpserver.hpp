#pragma once
/*
 * Stand-in for libhttpserver, declaring only what the plugin touches.
 *
 * Compiled BOTH ways: with PM_STUB_CONST_RETURN, render_GET returns
 * `const std::shared_ptr<http_response>` (libhttpserver's own shape); without
 * it, plain `std::shared_ptr<http_response>` (the shape FPP's drogon shim
 * uses).  The plugin must bind under both, which is the whole point — a
 * top-level cv mismatch on a virtual's return type is a hard error, so getting
 * it wrong would build cleanly on one FPP line and refuse to build on the
 * other.
 */

#include <memory>
#include <string>
#include <vector>

namespace httpserver {

class http_request {
public:
    std::string get_path() const { return m_path; }
    const std::vector<std::string>& get_path_pieces() const { return m_pieces; }
    std::string get_arg(const std::string&) const { return std::string(); }
    std::string get_content() const { return std::string(); }
    std::string get_querystring() const { return std::string(); }

private:
    std::string m_path;
    std::vector<std::string> m_pieces;
};

class http_response {
public:
    virtual ~http_response() = default;
};

class string_response : public http_response {
public:
    string_response(const std::string& body, int code = 200,
                    const std::string& contentType = "text/plain")
        : body(body), code(code), contentType(contentType) {}

    std::string body;
    int code;
    std::string contentType;
};

#ifdef PM_STUB_CONST_RETURN
#define PM_STUB_RESP const std::shared_ptr<http_response>
#else
#define PM_STUB_RESP std::shared_ptr<http_response>
#endif

class http_resource {
public:
    virtual ~http_resource() = default;
    virtual PM_STUB_RESP render_GET(const http_request&) { return nullptr; }
    virtual PM_STUB_RESP render_POST(const http_request&) { return nullptr; }
    virtual PM_STUB_RESP render_PUT(const http_request&) { return nullptr; }
    virtual PM_STUB_RESP render_DELETE(const http_request&) { return nullptr; }
    virtual PM_STUB_RESP render_HEAD(const http_request& req) { return render_GET(req); }
};

class webserver {
public:
    void register_resource(const std::string&, http_resource*, bool = false) {}
    void unregister_resource(const std::string&, http_resource*) {}
    void unregister_resource(const std::string&) {}
};

} // namespace httpserver
