# Todo API — C++

A TodoMVC-style CRUD REST API in C++, built **twice** against the same contract as the
[C version](../C):

| Folder | Approach | What it shows |
|---|---|---|
| [`no-framework/`](./no-framework) | raw POSIX sockets, hand-written HTTP and JSON | what C++ changes over C, and what it leaves exactly as C |
| [`framework/`](./framework) | the [Crow](https://crowcpp.org) micro-framework | what a framework deletes, and what it adds |

Both store todos in **SQLite**, so they survive a server restart.

## Build & run — `no-framework/`

Requires a C++20 compiler (for `std::format`) and the SQLite library.

```bash
cd no-framework
make            # compiles each module, links into ./server with -lsqlite3
./server
```

`make` builds incrementally — change one file and only that module recompiles.
To build without make: `g++ -std=c++20 -Wall -Wextra *.cpp -o server -lsqlite3`.

On macOS:
- If the system `g++` (Apple Clang) lacks `std::format`, use Homebrew GCC
  (`brew install gcc`, then `make CXX=g++-14`).
- If the SQLite header or library isn't found, `brew install sqlite` and point the
  build at it:
  ```bash
  make CXXFLAGS="-std=c++20 -Wall -Wextra -I$(brew --prefix sqlite)/include" \
       LDLIBS="-L$(brew --prefix sqlite)/lib -lsqlite3"
  ```

> **Run from the folder that holds `openapi.json`.** The server reads the spec, and
> opens `todos.db`, with *relative* paths, so both resolve against your current working
> directory — not where the binary lives.

The database file `todos.db` is created on first run. It's your data, not source code,
so it's git-ignored. Delete it to start with an empty list.

## Build & run — `framework/` (Crow)

Requires a C++20 compiler, **CMake 3.16+**, and the SQLite library (ships with macOS).
CMake downloads Crow and its networking dependency, Asio, on the first configure — there's
nothing else to install.

```bash
cd framework
cmake -B build && cmake --build build
./build/todo_crow
```

The first configure takes a minute (it clones Crow and Asio into `build/_deps/`). After
that, `cmake --build build` only recompiles what changed.

Things that bit during setup, and why:

- **Asio is pinned to 1.30.2.** Crow 1.2.0 still uses `asio::io_service`, which newer Asio
  releases removed. A Homebrew `asio` in `/usr/local/include` can shadow the pinned copy
  and bring the `io_service` errors back — if they reappear, `brew uninstall asio`.
- **SQLite target name.** Newer CMake calls it `SQLite3::SQLite3`; older CMake only knows
  `SQLite::SQLite3`. Switch the name in `CMakeLists.txt` if CMake says the target isn't found.
- **VS Code can't find `crow.h`.** CMake writes `build/compile_commands.json`; point the
  C/C++ extension at it in `.vscode/settings.json`:
  ```json
  { "C_Cpp.default.compileCommands": "${workspaceFolder}/build/compile_commands.json" }
  ```

Paths: `openapi.json` is found via an **absolute path baked in by CMake** (`OPENAPI_PATH`),
so it works from any folder. `todos.db` is still **relative** — it's created in whatever
folder you start the server from.

## Project layout — `no-framework/`

Split into one module per concern — headers (`.h`) declare, sources (`.cpp`) define.

```
no-framework/
├── model.h            # the data types: Todo, BoolField (header-only)
├── todo_repository.h  # the TodoRepository interface: all, get, add, update, remove
├── sqlite_todo_repository.h / .cpp  # the SQLite implementation of that interface
├── uuid.h / .cpp      # generate_uuid, is_valid_uuid
├── http.h / .cpp      # Status, status_line, send_response, read_file, content types
├── json.h / .cpp      # todo_to_json, todos_to_json, json_get_string, json_get_bool
├── signals.h / .cpp   # the SIGINT shutdown flag + handler
├── handlers.h / .cpp  # one function per route
├── main.cpp           # builds the repository, then socket setup, accept loop, routing
├── Makefile
└── openapi.json
```

A header *announces* what exists; exactly one `.cpp` *defines* it.

## Project layout — `framework/`

```
framework/
├── CMakeLists.txt   # fetches Crow + Asio, links SQLite, defines OPENAPI_PATH
├── main.cpp         # everything: Todo, TodoRepository (SQLite), helpers, routes
└── openapi.json
```

One file on purpose: with the socket loop, HTTP parser and router gone, what's left is
small enough to read top to bottom.

## The repository and dependency injection (`no-framework/`)

Storage sits behind an interface:

```cpp
class TodoRepository {
public:
    virtual std::vector<Todo>   all() = 0;
    virtual std::optional<Todo> get(const std::string& id) = 0;
    virtual bool add(const Todo& t) = 0;
    virtual bool update(const Todo& t) = 0;
    virtual bool remove(const std::string& id) = 0;
    virtual ~TodoRepository() = default;
};
```

`SqliteTodoRepository` implements it. The handlers take a `TodoRepository&` — the
interface, not the SQLite class — so they never see SQL or the database connection.
They're handed their storage instead of reaching for it: that's dependency injection.

`main` is the only place that names SQLite:

```cpp
int main() {
    try {
        SqliteTodoRepository repo("todos.db");   // constructor opens the db
        return run_server(repo);                 // inject it
    }                                            // destructor closes the db
    catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
}
```

The database's lifetime is the repository object's lifetime (RAII), so there's no
`close` call to forget. If the database can't be opened, the constructor throws and the
server never starts.

## Endpoints

Both versions listen on **port 8080**. Same contract as every language in the polyglot project.

| Verb | Path | Does | Success |
|---|---|---|---|
| `GET` | `/todos` | list all | 200 |
| `POST` | `/todos` | create (body: `{"title": "..."}`) | 201 |
| `GET` | `/todos/{id}` | fetch one | 200 |
| `PUT` | `/todos/{id}` | full replace (title + completed both required) | 200 |
| `PATCH` | `/todos/{id}` | partial update (any subset of fields) | 200 |
| `DELETE` | `/todos/{id}` | remove | 204 |
| `GET` | `/openapi.json` | the OpenAPI spec | 200 |
| `GET` | `/docs` | Swagger UI | 200 |

Errors: `400` (invalid id or malformed body), `404` (no such todo), `500` (spec file
unreadable, or the database failed to save). In the Crow version, an unknown path also
gets a JSON `404` from a catch-all route, and a database error becomes a `500` because
Crow turns any uncaught exception into one.

```bash
curl localhost:8080/todos
curl -X POST localhost:8080/todos -d '{"title":"Buy milk"}'
curl -X PATCH localhost:8080/todos/<id> -d '{"completed":true}'
curl -X DELETE localhost:8080/todos/<id>
```

## Persistence with SQLite

SQLite is a database that's just a file — no server to run. In both versions, the
repository's constructor opens `todos.db` and creates the table if it's
missing:

```sql
CREATE TABLE IF NOT EXISTS todos (
  id        TEXT PRIMARY KEY,
  title     TEXT NOT NULL,
  completed INTEGER NOT NULL   -- SQLite has no bool: 0/1
);
```

Every query uses a **prepared statement**, in four steps:

1. **prepare** — compile the SQL, with `?` placeholders for values
2. **bind** — fill the placeholders (`sqlite3_bind_text`, `sqlite3_bind_int`)
3. **step** — run it (one step for INSERT/UPDATE/DELETE; one step *per row* for SELECT)
4. **finalize** — free the statement

Values are always bound, never pasted into the SQL string. That's what makes the store
immune to SQL injection: a title like `'); DROP TABLE todos; --` is stored as a title.

Updates follow a **fetch, modify, write back** pattern. The repository returns a *copy*
(`std::optional<Todo>`), the handler changes the copy, and `update` saves it. PUT
and PATCH differ only in how the handler builds the copy — the repository writes the
whole row either way.

In the Crow version, a small private `Statement` class wraps those four steps: its
constructor prepares, `bind_text` / `bind_int` bind, `step()` runs, and its **destructor
finalizes** — so an early return or an exception can't leak a statement.

## What C++ changed over the C version

Same protocol work — the sockets, the raw HTTP text, and the signal handling are
identical, because those belong to the operating system, not the language. What C++
changed is the **materials**:

| C | C++ |
|---|---|
| `char title[256]` (fixed, can overflow) | `std::string` (grows, no overflow) |
| `Todo todos[100]; int count;` | a SQLite table (no cap, survives restarts) |
| `strcmp(path, "/todos")` | `path == "/todos"` (value comparison) |
| return `0/1` + out-param | `std::optional<...>` ("maybe missing" in the type) |
| `const char* STATUS_200 = "200 OK"` | `enum class Status` (type-safe, can't pass a wrong value) |
| `snprintf` into a buffer | `std::format` (type-safe, grows to fit) |
| `arc4random_buf` (BSD only) | `<random>` (portable) |

## What Crow changed over the hand-written version

| `no-framework/` | `framework/` (Crow) |
|---|---|
| `socket` / `bind` / `listen` / `accept` loop | `app.port(8080).multithreaded().run()` |
| parse the request line and headers by hand | Crow parses; handlers get a `crow::request` |
| `if (method == ... && path == ...)` routing | `CROW_ROUTE(app, "/todos/<string>").methods(...)` — the `<string>` arrives as a parameter |
| naive hand-written JSON parser | `crow::json::load` → `rvalue` (read) and `wvalue` (write) |
| `status_line` + `send_response` | `crow::response(code, body)` |
| one request at a time | a **thread pool** — requests run concurrently |
| `Makefile` | `CMakeLists.txt` with `FetchContent` for dependencies |

The cost of the thread pool: several requests can now hit the one SQLite connection at
the same moment. `TodoRepository` owns a `std::mutex`, and every method takes a
`std::lock_guard` before touching the database — the routes never lock anything
themselves.

## Design notes worth interrogating

- Why do the handlers take `TodoRepository&` and not `SqliteTodoRepository&`? (So they
  depend on what storage can *do*, not how it's done. Any implementation can be plugged in.)
- What do `virtual`, `= 0` and `override` each guarantee? (Runtime dispatch to the real
  object; every implementation must provide the function; a typo in a signature is a
  compile error instead of a silent new function.)
- Why does `SqliteTodoRepository` delete its copy constructor? (It owns a database
  connection — two copies would both close it.)
- Why does `get` return a `std::optional<Todo>` *copy* and not a pointer? (The data
  lives in a file — there's nothing in memory to point at. Changing the copy changes
  nothing until `update` writes it back.)
- `update` and `remove` check `sqlite3_changes`. Why? (An UPDATE or DELETE that matches
  no rows still "succeeds" — zero rows changed is how you detect a missing id.)
- Bind positions start at **1**, result columns at **0**. Same library, two conventions.
- `json_get_bool` returns `{present, valid, value}`, not a plain `std::optional<bool>`.
  Why three states? (PATCH needs to tell "absent" from "garbage"; PUT doesn't.)
- The handlers use early-`return` guard clauses, so there's no nested `else` and exactly
  one response per request — plus one `close(client_fd)` at the bottom of the loop.
- The JSON parser is deliberately naive (no real parsing, no escape handling). It trusts
  well-formed input — fine for learning, and a good thing to compare against a real parser.
- `#pragma once`, `inline constexpr` for header constants, and the compile-versus-link
  split (`-lsqlite3` only matters at link time) — the module split is where those stop
  being trivia and start mattering.

Crow version:

- Why does `DELETE` not call `find` first? (`remove` reports `sqlite3_changes`; zero rows
  deleted *is* the 404. One query, and no gap between "check" and "delete" for another
  request to slip into.)
- Why does PATCH validate every field before changing any? (So a body like
  `{"title":"X","completed":"yes"}` returns `400` *and* changes nothing.)
- Why does the mutex live inside the repository, not in the routes? (The repository owns
  the connection, so it protects it. A route can't forget to lock.)
- PATCH still does fetch-then-write as two locked steps. Two simultaneous PATCHes on the
  same todo can overwrite each other ("last write wins"). Acceptable here; how would you
  close that gap?
- `crow::json::wvalue` vs `rvalue`, `std::move` into the response array, a raw string
  literal `R"(...)"` for the Swagger page — small C++ idioms the framework version
  introduces.

## Roadmap

- [x] **SQLite persistence** — todos survive a restart, using prepared statements.
- [x] **A repository for `Todo`** — a `TodoRepository` interface, implemented with
      SQLite and injected into the handlers.
- [ ] **An in-memory repository** — a second `TodoRepository` backed by a vector, swapped
      in with one line in `main`, without touching a handler.
- [x] **A framework version (Crow)** — the same contract with a micro-framework: it
      deletes the socket loop, the parser and the router, and adds a thread pool, so
      concurrent requests hit the same database connection (guarded by a mutex).
- [ ] **Dependency injection in the Crow version** — today it uses a concrete
      `TodoRepository` held in a global; move to the same interface + injection as
      `no-framework/`.
- [ ] **Split the Crow version into modules** — once it outgrows one file.