#pragma once

#include "Json.h"
#include "Preview.h"

namespace arch::api {
// Include the terminating newline in the transport budget. Never emit a prefix
// of the failed JSON. Keep confirmed state whenever it fits in the error reply.
inline PreviewResponse SerializePreviewResponse(detail::Json &response, int exit_code) {
    try { return {response.dump(max_response_bytes - 1), exit_code}; }
    catch (const std::length_error &) {}
    response["status"] = "error";
    response["stage"] = "response";
    response["data"] = detail::Json();
    if (response.contains("graphicalBindings"))
        response["graphicalBindings"] = detail::Json::object({{"version", "1"}, {"items", detail::Json::array()}});
    response["diagnostics"] = detail::Json::array({detail::Json::object({
        {"severity", "error"}, {"code", "RESPONSE_TOO_LARGE"},
        {"message", "Response exceeds 8 MiB; reduce the request size or preview sample count."}})});
    // Configuration inspection may echo many short custom keys. Its metadata
    // can exceed the transport budget even though stdin was bounded to 1 MiB.
    if (response.contains("customParameters")) response.erase("customParameters");
    try { return {response.dump(max_response_bytes - 1), 7}; }
    catch (const std::length_error &) {}
    response.erase("parameterMetadata");
    if (response.contains("parameters")) response.erase("parameters");
    response["state"] = detail::Json();
    response["diagnostics"].push(detail::Json::object({{"severity", "warning"},
        {"code", "STATE_OMITTED_FOR_SIZE"}, {"message", "The state/metadata alone exceeds the response budget and was omitted."}}));
    return {response.dump(max_response_bytes - 1), 7};
}
} // namespace arch::api
