#pragma once
#include <string>
#include <sqlite3.h>
#include "todo_repository.h"

class SqliteTodoItems : public TodoItems {
public:
    explicit SqliteTodoItems(const std::string& path); // opens the db
    ~SqliteTodoItems() override;  // closes it

    // Owns a database connection, so copying is forbidden (two copies = closed twice)
    SqliteTodoItems(const SqliteTodoItems&) = delete;
    SqliteTodoItems& operator=(const SqliteTodoItems&) = delete;

    std::vector<Todo>    all() override;
    std::optional<Todo>  get(const std::string& id) override; 
    bool add(const Todo& t) override;
    bool update(const Todo& t) override;
    bool remove(const std::string& id) override;

private:
    sqlite3* db_ = nullptr; // was sqlite3* db in store.cpp
};
