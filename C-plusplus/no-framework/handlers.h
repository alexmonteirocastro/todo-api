#pragma once
// ============================================================================
// routes.h — one function per route. main.cpp dispatches to these.
// ============================================================================
#include <string>

void handle_get_all(int fd);
void handle_post(int fd, const std::string& body);
void handle_get_one(int fd, const std::string& id);
void handle_put(int fd, const std::string& id, const std::string& body);
void handle_patch(int fd, const std::string& id, const std::string& body);
void handle_delete(int fd, const std::string& id);
void handle_open_api(int fd);
void handle_docs(int fd);