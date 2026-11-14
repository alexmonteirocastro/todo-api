# Todo API — C++

A TodoMVC-style CRUD REST API written **from scratch** in C++ — raw POSIX sockets,
hand-written HTTP and JSON, no framework. The same contract as the [C version](..ls /C),
rebuilt to see what C++ changes and what it leaves exactly as C.

Single file: `main.cpp`. No framework, no external libraries — just the standard
library and the OS socket API.

## Build & run

Requires a C++20 compiler (for `std::format`).

```bash
g++ -std=c++20 -Wall -Wextra -o server main.cpp
./server
```

On macOS, if the system `g++` (Apple Clang) lacks `std::format`, use Homebrew GCC:

```bash
brew install gcc
g++-14 -std=c++20 -Wall -Wextra -o server main.cpp
```

> **Run from the folder that holds `openapi.json`.** The server reads the spec with a
> *relative* path, so `/openapi.json` resolves the file against your current working
> directory — not where the binary lives.

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

Errors: `400` (invalid id or malformed body), `404` (no such todo), `500` (spec file
unreadable).

```bash
curl localhost:8080/todos
curl -X POST localhost:8080/todos -d '{"title":"Buy milk"}'
curl -X PATCH localhost:8080/todos/<id> -d '{"completed":true}'
curl -X DELETE localhost:8080/todos/<id>
```

## What C++ changed over the C version

Same protocol work — the sockets, the raw HTTP text, and the `sigaction` shutdown are
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

The store is a single in-memory `std::vector<Todo>` — todos do **not** survive a restart
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

## Roadmap

- [ ] **SQLite persistence** — replace the in-memory vector so todos survive a restart,
      using prepared statements.
- [ ] **A small custom ORM** — wrap the SQL behind a repository (`TodoRepository.save()`,
      `.all()`, `.find()`), so handlers never touch SQL directly. Learn what Prisma /
      SQLAlchemy do under the hood by building a tiny one.
- [ ] **A framework version (Crow)** — rebuild the same contract with a micro-framework
      to see what it deletes (the socket loop, the parser, the router) and what it adds
      (a thread pool → the shared store becomes a data race → a `std::mutex`).
- [ ] `/docs` — a Swagger UI page that renders `openapi.json` in the browser.