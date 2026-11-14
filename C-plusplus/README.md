# Todo API — C++

A TodoMVC-style CRUD REST API written **from scratch** in C++ — raw POSIX sockets,
hand-written HTTP and JSON, no framework. The same contract as the [C version](../C),
rebuilt to see what C++ changes and what it leaves exactly as C.

The code lives in `no-framework/`, split into modules by concern. (A `framework/`
version using Crow is planned — see Roadmap.)

## Build & run

Requires a C++20 compiler (for `std::format`).

```bash
cd no-framework
make            # compiles each module, links into ./server
./server
```

`make` builds incrementally — change one file and only that module recompiles.
To build without make: `g++ -std=c++20 -Wall -Wextra *.cpp -o server`.

On macOS, if the system `g++` (Apple Clang) lacks `std::format`, use Homebrew GCC
(`brew install gcc`, then `make CXX=g++-14`).

> **Run from the folder that holds `openapi.json`.** The server reads the spec with a
> *relative* path, so `/openapi.json` resolves the file against your current working
> directory — not where the binary lives.

## Project layout

Split into one module per concern — headers (`.h`) declare, sources (`.cpp`) define.

```
no-framework/
├── model.h            # the data types: Todo, BoolField (header-only)
├── store.h / .cpp     # the in-memory todo store + find_todo_item  (← SQLite lands here)
├── uuid.h / .cpp      # generate_uuid, is_valid_uuid
├── http.h / .cpp      # Status, status_line, send_response, read_file, content types
├── json.h / .cpp      # todo_to_json, todos_to_json, json_get_string, json_get_bool
├── signals.h / .cpp   # the SIGINT shutdown flag + handler
├── handlers.h / .cpp  # one function per route (the "routers")
├── main.cpp           # socket setup, the accept loop, and routing
├── Makefile
└── openapi.json
```

A header *announces* what exists; exactly one `.cpp` *defines* it. The shared store is
declared `extern std::vector<Todo> todos;` in `store.h` and defined once in `store.cpp`.

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
unreadable).

```bash
curl localhost:8080/todos
curl -X POST localhost:8080/todos -d '{"title":"Buy milk"}'
curl -X PATCH localhost:8080/todos/<id> -d '{"completed":true}'
curl -X DELETE localhost:8080/todos/<id>
```

## What C++ changed over the C version

Same protocol work — the sockets, the raw HTTP text, and the signal handling are
identical, because those belong to the operating system, not the language. What C++
changed is the **materials**:

| C | C++ |
|---|---|
| `char title[256]` (fixed, can overflow) | `std::string` (grows, no overflow) |
| `Todo todos[100]; int count;` | `std::vector<Todo>` (no cap, `.size()` knows) |
| `strcmp(path, "/todos")` | `path == "/todos"` (value comparison) |
| return `0/1` + out-param | `std::optional<std::string>` ("maybe missing" in the type) |
| `const char* STATUS_200 = "200 OK"` | `enum class Status` (type-safe, can't pass a wrong value) |
| `snprintf` into a buffer | `std::format` (type-safe, grows to fit) |
| `arc4random_buf` (BSD only) | `<random>` (portable) |

The store is an in-memory `std::vector<Todo>` — todos do **not** survive a restart
(see Roadmap).

## Design notes worth interrogating

- Why does `find_todo_item` return a `Todo*` and not a `Todo` copy? (So handlers mutate
  the real stored item — and `nullptr` is a free "not found".)
- `json_get_bool` returns `{present, valid, value}`, not a plain `std::optional<bool>`.
  Why three states? (PATCH needs to tell "absent" from "garbage"; PUT doesn't.)
- The handlers use early-`return` guard clauses, so there's no nested `else` and exactly
  one response per request — plus one `close(client_fd)` at the bottom of the loop.
- The JSON parser is deliberately naive (no real parsing, no escape handling). It trusts
  well-formed input — fine for learning, and a good thing to compare against a real parser.
- `extern` vs definition, `#pragma once`, `inline constexpr` for header constants — the
  split is where those stop being trivia and start mattering.

## Roadmap

- [ ] **SQLite persistence** — replace the in-memory vector so todos survive a restart,
      using prepared statements. Only `store.cpp` changes; handlers don't move.
- [ ] **A small custom ORM** — wrap the SQL behind a repository (`TodoRepository.save()`,
      `.all()`, `.find()`), injected into the handlers (dependency injection) so they
      depend on an interface, not the concrete store. Build what Prisma / SQLAlchemy are.
- [ ] **A framework version (Crow)** — rebuild the same contract with a micro-framework
      to see what it deletes (the socket loop, the parser, the router) and what it adds
      (a thread pool → the shared store becomes a data race → a `std::mutex`).