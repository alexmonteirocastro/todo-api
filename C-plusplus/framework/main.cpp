#include "crow.h"
#include <cstdio>
#include <string>
#include <vector>
#include <cctype>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <sqlite3.h>
#include <stdexcept>
#include <mutex>
#include <optional>

/**
 * CMake normally defines this with the absolute path to openapi.json.
 * This fallback is used if it isn't defined (and keeps the editor quiet).
 */
#ifndef OPENAPI_PATH
#define OPENAPI_PATH "openapi.json"
#endif 

constexpr int PORT = 8080; 

// Defines what a TODO item is - Matches the contract: id, title, completed
struct Todo { 
    std::string id;
    std::string title;
    bool completed;
};


// Only place that talks to  SQLite
class TodoRepository {
    public: 
        explicit TodoRepository(const std::string& path) {
            if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
                std::string msg = sqlite3_errmsg(db_);
                sqlite3_close(db_);
                throw std::runtime_error("cannot open database: " + msg);
            }

            const char* sql = 
                "CREATE TABLE IF NOT EXISTS todos ("
                "   id          TEXT PRIMARY KEY,"
                "   title       TEXT NOT NULL,"
                "   completed   INTEGER NOT NULL DEFAULT 0)";

            char* err = nullptr;
            if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
                std::string msg = err;
                sqlite3_free(err);
                sqlite3_close(db_);
                throw std::runtime_error("cannot create table: " + msg);
            }
        }

        ~TodoRepository() { sqlite3_close(db_); }

        // It owns the database conection, so copying it makes no sense
        TodoRepository(const TodoRepository&) = delete;
        TodoRepository& operator=(const TodoRepository&) = delete;

        void insert(const Todo& t) {
            std::lock_guard<std::mutex> lock(mutex_);
            Statement st(db_, "INSERT INTO todos (id, title, completed) VALUES (?, ?, ?)");
            st.bind_text(1, t.id);
            st.bind_text(2, t.title);
            st.bind_int(3, t.completed ? 1 : 0);
            st.step();
        }

        std::vector<Todo> list() {
            std::lock_guard<std::mutex> lock(mutex_);
            Statement st(db_, "SELECT id, title, completed FROM todos ORDER BY rowid");
            std::vector<Todo> result;
            while (st.step()) {
                result.push_back(row_to_todo(st));
            }
            return result;
            
        }

        /**
         * Look up a TODO item by id. 
         * Returns the TODO item, or "nothing", if it doesn't exist
        */
        std::optional<Todo> find(const std::string& id) {
            std::lock_guard<std::mutex> lock(mutex_);
            Statement st(db_, "SELECT id, title, completed FROM todos WHERE id = ?");
            st.bind_text(1, id);
            if (st.step()) return row_to_todo(st); // found a row
            return std::nullopt; // no row -> nothing
        }

        /**
         * Overwrite title and completed of an existng TODO item
         * Returns false if no TODO item has that id
         */
        bool update(const Todo &t) {
            std::lock_guard<std::mutex> lock(mutex_);
            Statement st(db_, "UPDATE todos SET title = ?, completed = ? WHERE id = ?");
            st.bind_text(1, t.title);
            st.bind_int(2, t.completed ? 1 : 0);
            st.bind_text(3, t.id);
            st.step();
            return sqlite3_changes(db_) > 0;
        }

        /**
         * Delete a TODO item
         * Returns false if no TODO item had this id
         */
        bool remove(const std::string& id) {
            std::lock_guard<std::mutex> lock(mutex_);
            Statement st(db_, "DELETE FROM todos WHERE id = ?");
            st.bind_text(1, id);
            st.step();
            return sqlite3_changes(db_) > 0;
        }

    private:
        sqlite3* db_ = nullptr;

        /**
         * Small wrapper around one prepared SQL statement
         * The destructor always finalizes it (RAII, like lock_guard)
         */
        class Statement {
            public: 
                Statement(sqlite3* db, const char* sql) : db_(db) {
                    if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
                        throw std::runtime_error(std::string("sql error: ") + sqlite3_errmsg(db));
                    }
                }

                ~Statement() { sqlite3_finalize(stmt_); }
                Statement(const Statement&) = delete;
                Statement& operator=(const Statement&) = delete;

                // ? placeholders are numbered from 1
                void bind_text(int i, const std::string& v) {
                    sqlite3_bind_text(stmt_, i, v.c_str(), -1, SQLITE_TRANSIENT);
                }
                void bind_int(int i, int v) {
                    sqlite3_bind_int(stmt_, i, v);
                }

                // true - a row is ready to read, false = finished
                bool step() {
                    int rc = sqlite3_step(stmt_);
                    if (rc == SQLITE_ROW) return true;
                    if (rc == SQLITE_DONE) return false;
                    throw std::runtime_error(std::string("sql error: ") + sqlite3_errmsg(db_));
                }

                std::string text(int col) const {
                    const unsigned char* p = sqlite3_column_text(stmt_, col);
                    return p ? reinterpret_cast<const char*>(p) : "";
                }
                int integer(int col) const {
                    return sqlite3_column_int(stmt_, col);
                }
            
            private:
                sqlite3* db_; // the connection, needed for error messages
                sqlite3_stmt* stmt_ = nullptr; // the compiled statement, SQLite's handle
        };

        // Turn the current row(id, title, completed) into a TODO item
        static Todo row_to_todo(const Statement& st) {
            return Todo{st.text(0), st.text(1), st.integer(2) != 0};
        }

        std::mutex mutex_; // one connection, several Crow threads -> take turns
};

TodoRepository repo("todos.db");


std::vector<Todo> todos = {};

// Turn one TODO item into a JSON object
crow::json::wvalue todo_to_json(const Todo& t) {
    crow::json::wvalue j;
    j["id"] = t.id;
    j["title"] = t.title;
    j["completed"] = t.completed;
    return j;
}

/**
 * Crow handles reqests on multiple threads
 * if two requests touch `/todos` at the same time (one adding, one reading)
 * that is a data race, so every handler locks this mutex, while it uses the vector
 */
std::mutex todos_mutex;

/* Generates a rando UUIDv4, e.g. 550e8400-e29b-41d4-a716-446655440000 */
std::string generate_uuid() {
    thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<int> nibble(0, 15);
    
    const char* hex = "0123456789abcdef";
    std::string id = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
    
    for (char& c : id) {
        if (c == 'x') {
            c = hex[nibble(rng)];
        } else if (c == 'y') {
            c = hex[8 + (nibble(rng) & 3)]; // variant bits: 8, 9, a or b
        }
    }
    return id;
}

/**
 * Vaidates if the provided string is a vlaid UUID
 * 8-4-4-4-12 hex digits
 * e.g. 550e8400-e29b-41d4-a716-446655440000
 */
bool is_valid_uuid(const std::string& id) {
    if (id.size() != 36) return false;
    
    for (size_t i=0; i < id.size(); i++) {
        char c = id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false; // dashes must be here
        } else if (!std::isxdigit(static_cast<unsigned char>(c))) { // anything else must be hex digit
            return false;
        }
    }
    
    return true;
}

// Read the whole file into a string. Returns false if it can't be opened 
bool read_file(const std::string& path, std::string& out) {
    std::ifstream file(path);
    if (!file) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

// The Swagger UI page. it loads the UI from a CDN and points it at /openapi.json
const char* SWAGGER_HTML = R"(<!DOCTYPE html>
        <html lang="en">
        <head>
            <meta charset="utf-8" />
            <title>TODO API Docs</title>
            <link rel="stylesheet" href="https://unpkg.com/swagger-ui-dist@5.11.0/swagger-ui.css" />
        </head>
        <body>
            <div id="swagger-ui"></div>
            <script src="https://unpkg.com/swagger-ui-dist@5.11.0/swagger-ui-bundle.js" crossorigin></script>
            <script>
                window.onload = () => {
                    window.ui = SwaggerUIBundle({ url: '/openapi.json', dom_id: '#swagger-ui' });
                };
            </script>
        </body>
        </html>
)";

bool is_json_bool(const crow::json::rvalue& v) {
    return v.t() == crow::json::type::True || v.t() == crow::json::type::False;
}

bool is_non_empty_string(const crow::json::rvalue& v) {
    return v.t() == crow::json::type::String && v.s().size() > 0;
}

/**
 * Builds a response with a status code, a JSOn body and a JSON header
 * Used in many places
 */
crow::response json_response(int status, const std::string& body) {
    crow::response res(status, body);
    res.set_header("Content-Type", "application/json");

    return res;
}

/**
 * Looks a TODO item up by its id
 * Returns a pointer to it, or nullptr if not found
 */
Todo* find_todo_by_id(const std::string& id) {
    for (Todo& t : todos) {
        if(t.id == id) return &t;
    }
    return nullptr;
}


int main() {
    crow::SimpleApp app;

    // When someone calls the GET /todos, run this function
    CROW_ROUTE(app, "/todos") ([]() {
        // Build a JSON Array from the Vector
        std::vector<crow::json::wvalue> items;
        for (const Todo& t : repo.list()) {
            items.push_back(todo_to_json(t));
        }
        crow::json::wvalue body = std::move(items);

        return json_response(200, body.dump());
    });

    // POST /todos
    CROW_ROUTE(app, "/todos").methods(crow::HTTPMethod::Post)([](const crow::request& req) {
        // 1. Parse the body. Not a valid JSON -> 400
        crow::json::rvalue body = crow::json::load(req.body);
        if (!body) {
            return json_response(400, "{\"error\": \"Bad request\"}");
        }

        // 2. Title must exist, be a string and not empty -> else -> 400
        if (!body.has("title") || body["title"].t() != crow::json::type::String || body["title"].s().size() == 0) {
            return json_response(400, "{\"error\": \"title is required\"}");
        }

        // 3. Create the TODO item: server picks the id, completed defaults to false
        Todo t{generate_uuid(), std::string(body["title"].s()), false};

        repo.insert(t);

        // 4. 201 Created, wuth the full TODO item
        return json_response(201, todo_to_json(t).dump());
    });

    // GET /todos/<id>
    CROW_ROUTE(app, "/todos/<string>") ([] (const std::string& id) {
        if (!is_valid_uuid(id)) return json_response(400, "{\"error\": \"Invalid id\"}");

        std::optional<Todo> found = repo.find(id);
        if (!found) return json_response(404, "{\"error\": \"TODO item not found\"}");

        return json_response(200, todo_to_json(*found).dump());
    });

    // PATCH /todos/<id>
    CROW_ROUTE(app, "/todos/<string>").methods(crow::HTTPMethod::Patch) ([](const crow::request& req, const std::string& id){
         if (!is_valid_uuid(id)) return json_response(400, "{\"error\": \"Invalid id\"}");

         std::optional<Todo> found = repo.find(id);
         if (!found) return json_response(404, "{\"error\": \"TODO item not found\"}");

         crow::json::rvalue body = crow::json::load(req.body);
        if (!body) {
            return json_response(400, "{\"error\": \"Bad request\"}");
        }

        // Validates everything first, change NOTHING until all checks pass
        bool has_title = body.has("title");
        bool has_completed = body.has("completed");

        if (has_title && !is_non_empty_string(body["title"])) {
            return json_response(400, "{\"error\": \"Title must be a non-empty string\"}");
        }
        if (has_completed && !is_json_bool(body["completed"])) {
            return json_response(400, "{\"error\": \"Completed must be true or false\"}");
        }

        //Now, it is safe to aplly the changes (Chnage our copy of the TODO item, then write it back)
        Todo updated = *found;
        if (has_title) updated.title = std::string(body["title"].s());
        if (has_completed) updated.completed = body["completed"].b();
        repo.update(updated);

        return json_response(200, todo_to_json(updated).dump());
    });

    // PUT /todos/<id>
    CROW_ROUTE(app, "/todos/<string>").methods(crow::HTTPMethod::Put) ([](const crow::request& req, const std::string& id){
         if (!is_valid_uuid(id)) return json_response(400, "{\"error\": \"Invalid id\"}");

         if (!repo.find(id)) return json_response(404, "{\"error\": \"TODO item not found\"}");

         crow::json::rvalue body = crow::json::load(req.body);
        if (!body) {
            return json_response(400, "{\"error\": \"Bad request\"}");
        }

        // Validates everything first, change NOTHING until all checks pass
        bool has_title = body.has("title");
        bool has_completed = body.has("completed");

        if (!has_title || !is_non_empty_string(body["title"])
            || !has_completed || !is_json_bool(body["completed"])
            ) 
        {
            return json_response(400, "{\"error\": \"Title and completed are required\"}");
        }
        

        //Now, it is safe to aplly the changes
        Todo replaced{id, std::string(body["title"].s()), body["completed"].b()};
        repo.update(replaced);

        return json_response(200, todo_to_json(replaced).dump());
    });

    // DELETE /todos/<id>
    CROW_ROUTE(app, "/todos/<string>").methods(crow::HTTPMethod::Delete) ([](const std::string& id){
        if (!is_valid_uuid(id)) return json_response(400, "{\"error\": \"Invalid id\"}");

        // remove() tells us whether a row was actually deleted
        if (!repo.remove(id)) return json_response(404, "{\"error\": \"TODO item not found\"}");
         
         // 204 No Content: and deliberately empty
         return crow::response(204);
    });

    // GET /openapi.json
    CROW_ROUTE(app, "/openapi.json") ([]() {
        std::string spec;
        if (!read_file(OPENAPI_PATH, spec)) return json_response(500, "{\"error\": \"openapi.json not found\"}");
        return json_response(200, spec);
    });

    // GET /docs - Swagger UI page
    CROW_ROUTE(app, "/docs") ([]() {
        crow::response res(200, SWAGGER_HTML);
        res.set_header("Content-Type", "text/html");
        return res;
    });


    // Catch-all: any path that did not match any of the above routes
    CROW_CATCHALL_ROUTE(app) ([](const crow::request&, crow::response& res){
        res.code = 404;
        res.set_header("Content-Type", "application/json");
        res.body = "{\"error\": \"Not Found\"}";
        res.end();
    });

    std::printf("Listening on http://localhost:%d\n", PORT);
    app.port(PORT).multithreaded().run();

    return 0;
}