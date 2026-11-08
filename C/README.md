# Todo API in C

A complete **TodoMVC-style CRUD REST API written from scratch in C** — no web framework, no HTTP library, no JSON library. Just raw POSIX sockets, the C standard library, and a lot of careful string handling.

This is the first implementation in a polyglot learning project: the same API contract, re-built across many languages (C++, Rust, Go, Node/Express/Nest, PHP/Laravel) to see what each layer of abstraction does — and hides.

## Why

Modern frameworks make building a REST API a few lines of code. This project goes the other way: implement everything by hand — the HTTP parsing, the JSON serialization, the routing, the memory management — to understand what `req.body`, `res.json()`, and `@app.get()` are actually doing underneath.

## Features

- **Pure C** — only POSIX sockets and the standard library, no dependencies
- **Full CRUD** — six endpoints with correct HTTP verbs and status codes
- **HTTP by hand** — request parsing and response building from raw bytes
- **JSON by hand** — parsing request bodies and serializing responses without a library
- **UUIDv4 generation** — spec-compliant, from 16 random bytes (`arc4random_buf`)
- **In-memory store** — a fixed array of structs with manual memory management
- **Input validation** — UUID format checks, required-field enforcement
- **Graceful shutdown** — `SIGINT` handler for a clean exit (via `sigaction`)
- **Interactive docs** — Swagger UI served at `/docs` from a hand-written OpenAPI spec

## The API

| Method   | Path          | Description             | Success |
| -------- | ------------- | ----------------------- | ------- |
| `GET`    | `/todos`      | List all todos          | `200`   |
| `POST`   | `/todos`      | Create a todo           | `201`   |
| `GET`    | `/todos/{id}` | Fetch a single todo     | `200`   |
| `PATCH`  | `/todos/{id}` | Partial update          | `200`   |
| `PUT`    | `/todos/{id}` | Full replace (strict)   | `200`   |
| `DELETE` | `/todos/{id}` | Delete a todo           | `204`   |

Errors: `400` (malformed request / invalid id), `404` (no such todo), `500` (server error).

**`PATCH` vs `PUT`:** `PATCH` is a partial update — send only the fields you want to change. `PUT` is a strict full replace — both `title` and `completed` are required, or it returns `400`.

### Todo schema

```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "title": "Buy milk",
  "completed": false
}
```

- `id` — a UUIDv4, assigned by the server (never sent by the client)
- `title` — string, required on create
- `completed` — boolean, defaults to `false` on create

## Build & run

Requires a C compiler (`cc` / `clang` / `gcc`) on a POSIX system (macOS or Linux).

```bash
cc -Wall -Wextra -o server main.c
./server
```

The server listens on `http://localhost:8080`. Run it from the directory containing `openapi.json` so the docs route can find the spec.

Press `Ctrl-C` for a graceful shutdown.

## Usage

```bash
# Create a todo
curl -X POST http://localhost:8080/todos \
  -H "Content-Type: application/json" \
  -d '{"title":"Buy milk"}'

# List all todos
curl http://localhost:8080/todos

# Fetch one (use an id from a create response)
curl http://localhost:8080/todos/<id>

# Mark it done (partial update)
curl -X PATCH http://localhost:8080/todos/<id> \
  -d '{"completed":true}'

# Replace it entirely (strict)
curl -X PUT http://localhost:8080/todos/<id> \
  -d '{"title":"Buy oat milk","completed":false}'

# Delete it
curl -X DELETE http://localhost:8080/todos/<id>
```

## Interactive docs

With the server running, open **http://localhost:8080/docs** in a browser for the full Swagger UI — all endpoints, schemas, and a "Try it out" panel that hits the live server. The OpenAPI spec is served at `/openapi.json`.

## Design notes & deliberate simplifications

This is a learning project, and several choices trade robustness for clarity:

- **Single-threaded & blocking** — requests are handled one at a time. Fast here (each takes microseconds), but a slow request would block the queue. Concurrency (threads / an event loop) is intentionally left out.
- **In-memory store** — todos live in a fixed array (`MAX_TODOS = 100`) and are lost on restart. No database or file persistence.
- **Hand-rolled JSON parsing** — uses string search (`strstr` / `strchr`), which is deliberately crude: it assumes well-formed input and does not handle escaped quotes or nested objects. A real server would use a JSON library (e.g. cJSON).
- **Single `read()` per request** — assumes the full request arrives in one read, which holds for small local requests but isn't guaranteed over TCP.
- **Hand-written OpenAPI spec** — C can't introspect its own routes the way FastAPI reads Python type hints, so `openapi.json` is maintained by hand.

## Project structure

```
.
├── main.c         # the entire server
├── openapi.json   # OpenAPI 3.0 spec (served at /openapi.json)
└── README.md
```

## Possible next steps

- A table-driven router (routes as data) to auto-generate the OpenAPI spec
- `fork()` or thread-pool concurrency
- File or SQLite persistence
- The same API in the other languages (C++, Rust, Go, Node, …)

## License

MIT