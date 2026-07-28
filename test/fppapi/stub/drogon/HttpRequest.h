#pragma once
/* Stand-in for drogon, so FPP's real fpphttp.h can be compiled verbatim. */

#include <memory>
#include <string>
#include <string_view>

namespace drogon {

enum HttpMethod { Get, Post, Put, Delete, Head, Invalid };

enum HttpStatusCode { k200OK = 200 };

enum ContentType { CT_NONE, CT_CUSTOM, CT_APPLICATION_JSON, CT_TEXT_PLAIN };

class HttpRequest {
public:
    std::string path() const { return m_path; }
    std::string getParameter(const std::string&) const { return std::string(); }
    std::string_view body() const { return std::string_view(m_body); }
    std::string query() const { return std::string(); }
    HttpMethod method() const { return Get; }

private:
    std::string m_path;
    std::string m_body;
};

using HttpRequestPtr = std::shared_ptr<HttpRequest>;

} // namespace drogon
