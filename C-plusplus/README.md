# Todo API — C++

The same TodoMVC-style CRUD API as the C version, built **twice** in C++ to see the
cost and benefit of a framework:

| | `no-framework/` | `framework/` |
|---|---|---|
| HTTP server | hand-written: `socket`/`bind`/`listen`/`accept` loop | Crow (`app.port(8080).run()`) |
| Request parsing | `read()` raw bytes, split on `\r\n\r\n` | `crow::request`, `req.body` |
| Routing | one big `if/else` on method + path | `CROW_ROUTE(...).methods(...)` |
| Path params | `path.substr("/todos/")` | `/todos/<string>` capture |
| JSON in | hand-rolled key scanner (no real parse) | `crow::json::load` (real parse) |
| JSON out | string concatenation (**no escaping**) | `crow::json::wvalue` (**escapes**) |
| Concurrency | single-threaded, no locks needed | multithreaded → needs `std::mutex` |
| Ctrl-C | our own `sigaction` | Crow installs its own |
| Lines of code | ~370 | ~230 |

Both expose the identical contract (see `../C/openapi.json`): port **8080**, routes
`GET/POST /todos`, `GET/PATCH/PUT/DELETE /todos/{id}`, plus `GET /openapi.json` and
`GET /docs` (Swagger UI). Both pass the same 16-check lifecycle test.

## Build & run

**No framework** (nothing to install):
```bash
cd no-framework
make run          # g++ -std=c++17 -Wall -Wextra -O2
```

**Framework (Crow)** — needs standalone Asio + the amalgamated header:
```bash
sudo apt-get install libasio-dev     # macOS: brew install asio
cd framework
make run          # `make` auto-downloads crow_all.h if missing
```

Then, against either:
```bash
curl localhost:8080/todos
curl -X POST localhost:8080/todos -d '{"title":"Buy milk"}'
open http://localhost:8080/docs
```
Put `openapi.json` in the folder you run from so `/openapi.json` and `/docs` work.

## The point of doing it both ways

The no-framework build is the honest one for learning: **you are the framework**. You
still write every byte of HTTP and JSON — C++ only hands you safer materials for it
(`std::string` instead of `char[256]`, `std::vector` instead of a fixed array, RAII
instead of hand-matched `close()`, `std::optional` instead of NULL sentinels).

Crow then shows what a framework actually buys: it deletes the socket loop, the parser,
the router, and gives you *correct* JSON for free. But it also changes the shape of the
problem — its thread pool means the shared store is now a data race, so a `std::mutex`
appears. Nothing is free; the work moves.

### Things worth interrogating
- Why is the no-framework server's JSON output broken by a `"` in a title, but Crow's isn't?
- Why does only the Crow version need a mutex? What exactly races?
- `Fd` (RAII) vs. the C version's manual `close()` on every error branch — what can't you forget anymore?
- `std::optional<std::string>` from `json_get_string` vs. C's "return 0/1 + out-param" — where did the "maybe missing" go?
- Crow's `/todos/<string>` matches one segment. What does it do with `/todos/a/b`, and why?