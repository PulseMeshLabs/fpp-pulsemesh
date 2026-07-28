#pragma once
/* Stand-in for drogon, so FPP's real fpphttp.h can be compiled verbatim. */

#include "HttpRequest.h"

#include <memory>
#include <string>

namespace drogon {

class HttpResponse {
public:
    static std::shared_ptr<HttpResponse> newHttpResponse() {
        return std::make_shared<HttpResponse>();
    }
    void setStatusCode(HttpStatusCode) {}
    void setBody(const std::string&) {}
    void setContentTypeCode(ContentType) {}
    void setContentTypeCodeAndCustomString(ContentType, const std::string&) {}
};

using HttpResponsePtr = std::shared_ptr<HttpResponse>;

} // namespace drogon
