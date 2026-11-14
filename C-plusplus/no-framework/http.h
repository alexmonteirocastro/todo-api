#pragma once
// ============================================================================
// http.h — the HTTP response layer: status codes, content types, send + read.
// ============================================================================
#include <string>

enum class Status {
    Ok          = 200,
    Created     = 201,
    NoContent   = 204,
    BadRequest  = 400,
    NotFound    = 404,
    ServerError = 500
};

// `inline constexpr` (C++17+) lets a constant live in a header with ONE shared
// definition across all files that include it — no "multiple definition" error.
constexpr const char* CONTENT_JSON = "application/json";
constexpr const char* CONTENT_HTML = "text/html";

std::string status_line(Status s);
void send_response(int fd, const Status status, const char* type, const std::string& body);
std::string read_file(const std::string& path);