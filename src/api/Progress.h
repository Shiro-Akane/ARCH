#pragma once
#include "Json.h"
#include "Preview.h"
namespace arch::api {
inline void ReportStage(const PreviewRequest& request, detail::Json& result, const char* stage) {
    result["stage"] = stage;
    if (request.progress) request.progress(stage);
}
}
