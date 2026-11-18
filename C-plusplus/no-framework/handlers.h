#pragma once
// ============================================================================
// routes.h — one function per route. main.cpp dispatches to these.
// ============================================================================
#include <string>
#include "todo_repository.h"

void handle_get_all  (int fd, TodoItems& repo);
void handle_post     (int fd, TodoItems& repo, const std::string& body);
void handle_get_one  (int fd, TodoItems& repo, const std::string& id);
void handle_put      (int fd, TodoItems& repo, const std::string& id, const std::string& body);
void handle_patch    (int fd, TodoItems& repo, const std::string& id, const std::string& body);
void handle_delete   (int fd, TodoItems& repo, const std::string& id);
void handle_open_api (int fd);
void handle_docs     (int fd);