#include "store.h" // a .cpp always includes its own header first
#include <sqlite3.h>
#include <cstdio>

/** 
 * the connection handle. 
 * sqlite3 is an opaque struct from the library; 
 * we just hold a pointer to it. 
 * static makes it file-local 
 * */
static sqlite3* db = nullptr;

// The ONE real definition of the store. store.h only announced it (extern).
std::vector<Todo> todos; // The in memory store. Starts empty


bool store_init(const std::string& path) {
    /**
     * 1. Open and create the database file
     * opens the file (creates it if missing), fills in db. 
     * Returns SQLITE_OK (which is 0) on success. 
     * Note .c_str() — SQLite is a C library, so it wants const char*.
     */
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        std::fprintf(stderr, "store: can't open db: %s\n", sqlite3_errmsg(db));
        return false;
    }

    // 2. Create the table, if it isn't there yet
    const char* schema = 
        "CREATE TABLE IF NOT EXISTS todos (" // idempotent: first run makes the table, later runs do nothing. 
        "  id        TEXT PRIMARY KEY,"
        "  title     TEXT NOT NULL,"
        "  completed INTEGER NOT NULL" // SQLite has no boolean type, so we store a bool as 0/1. We’ll map bool ↔ int at the edges
        ");";

    char* err = nullptr;
    // runs SQL that returns no rows (like CREATE TABLE)
    if (sqlite3_exec(db, schema, nullptr, nullptr, &err) != SQLITE_OK) {
        std::fprintf(stderr, "store: can't create table: %s\n", err);
        sqlite3_free(err);
        return false;
    }
    
    return true;
}

void store_close() {
    sqlite3_close(db);
    db = nullptr;
}

bool store_add(const Todo& t) {
    const char* sql = "INSERT INTO todos (id, title, completed) VALUES (?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;

    // 1. PREPARE: compile the SQL into a statement object
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::fprintf(stderr, "store_add: prepare: %s\n", sqlite3_errmsg(db));
        return false;
    }

    // 2. BIND: fill in the ? placeholders (positions start at 1!)
    sqlite3_bind_text(stmt, 1, t.id.c_str(),        -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, t.title.c_str(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, t.completed ? 1 : 0);

    // 3. STEP: run it; An INSERT finishes in one step with SQLITE_DONE
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!ok) std::fprintf(stderr, "store_add step: %s\n", sqlite3_errmsg(db));

    // 4. FINALIZE: free the statement, on every path
    sqlite3_finalize(stmt);
    return ok;
}

/*Read the current row of a SELECT into a TODO item*/
static Todo row_to_todo(sqlite3_stmt* stmt) {
    Todo t;
    t.id        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    t.title     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    t.completed = sqlite3_column_int(stmt, 2) != 0; // int 0/1 -> bool
    return t;
}

std::vector<Todo> store_all() {
    std::vector<Todo> result;
    const char* sql = "SELECT id, title, completed FROM todos;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    
    // Each step gives one row (SQLITE_ROW) until there are no more (SQLITE_DONE)
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back(row_to_todo(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
}

std::optional<Todo> store_get(const std::string& id) {
    const char* sql = "SELECT id, title, completed FROM todos WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    
    std::optional<Todo> found;
    while (sqlite3_step(stmt) == SQLITE_ROW) { // the id is unique: 0 or 1 rows
        found = row_to_todo(stmt);
    }

    sqlite3_finalize(stmt);
    return found; // std::nullopt if nothing matched
}

bool store_update(const Todo& t) {
    const char* sql = "UPDATE todos SET title = ?, completed = ? WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::fprintf(stderr, "store_update prepare: %s\n", sqlite3_errmsg(db));
        return false;
    }
    sqlite3_bind_text(stmt, 1, t.title.c_str(), -1, SQLITE_TRANSIENT); // SET title = ?
    sqlite3_bind_int(stmt, 2, t.completed ? 1 : 0); // completed = ?
    sqlite3_bind_text(stmt, 3, t.id.c_str(), -1, SQLITE_TRANSIENT); //WHERE id = ?
    
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    

    // How many rows did that update actually change?
    return ok && sqlite3_changes(db) > 0;
}

bool store_remove(const std::string& id) {
    const char* sql = "DELETE FROM todos WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::fprintf(stderr, "store_remove prepare: %s\n", sqlite3_errmsg(db));
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT); //WHERE id = ?
    
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    

    // 0 rows deleted = no such id
    return ok && sqlite3_changes(db) > 0;
}