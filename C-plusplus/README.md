# Todo API — C++

A TodoMVC-style CRUD REST API written **from scratch** in C++ — raw POSIX sockets,
hand-written HTTP and JSON, no framework. The same contract as the [C version](../C),
rebuilt to see what C++ changes and what it leaves exactly as C.

Todos are stored in **SQLite**, so they survive a server restart.

The code lives in `no-framework/`, split into modules by concern. (A `framework/`
version using Crow is planned — see Roadmap.)

## Build & run

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

## Project layout

Split into one module per concern — headers (`.h`) declare, sources (`.cpp`) define.

```
no-framework/
├── model.h            # the data types: Todo, BoolField (header-only)
├── store.h / .cpp     # the SQLite store: init, add, all, get, update, remove, close
├── uuid.h / .cpp      # generate_uuid, is_valid_uuid
├── http.h / .cpp      # Status, status_line, send_response, read_file, content types
├── json.h / .cpp      # todo_to_json, todos_to_json, json_get_string, json_get_bool
├── signals.h / .cpp   # the SIGINT shutdown flag + handler
├── handlers.h / .cpp  # one function per route
├── main.cpp           # socket setup, the accept loop, and routing
├── Makefile
└── openapi.json
```

A header *announces* what exists; exactly one `.cpp` *defines* it. The handlers only
talk to the store through its functions — they never see SQL or the database
connection, which is private to `store.cpp`.

## Endpoints

Listens on **port 8080**. Same contract as every language in the polyglot project.

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
unreadable, or the database failed to save).

```bash
curl localhost:8080/todos
curl -X POST localhost:8080/todos -d '{"title":"Buy milk"}'
curl -X PATCH localhost:8080/todos/<id> -d '{"completed":true}'
curl -X DELETE localhost:8080/todos/<id>
```

## Persistence with SQLite

SQLite is a database that's just a file — no server to run. `store.cpp` opens
`todos.db` at startup and creates the table if it's missing:

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

Updates follow a **fetch, modify, write back** pattern. `store_get` returns a *copy*
(`std::optional<Todo>`), the handler changes the copy, and `store_update` saves it. PUT
and PATCH differ only in how the handler builds the copy — the store writes the whole
row either way.

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

## Design notes worth interrogating

- Why does `store_get` return a `std::optional<Todo>` *copy* and not a pointer? (The data
  lives in a file — there's nothing in memory to point at. Changing the copy changes
  nothing until `store_update` writes it back.)
- `store_update` and `store_remove` check `sqlite3_changes`. Why? (An UPDATE or DELETE
  that matches no rows still "succeeds" — zero rows changed is how you detect a missing id.)
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

## Roadmap

- [x] **SQLite persistence** — todos survive a restart, using prepared statements.
- [ ] **A small custom ORM** — wrap the store in a repository (`TodoRepository.save()`,
      `.all()`, `.find()`), injected into the handlers (dependency injection) so they
      depend on an interface, not the concrete store. Build what Prisma / SQLAlchemy are.
- [ ] **A framework version (Crow)** — rebuild the same contract with a micro-framework
      to see what it deletes (the socket loop, the parser, the router) and what it adds
      (a thread pool → concurrent requests hit the same database connection).