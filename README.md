# Todo API

The **same CRUD REST API, built from scratch in many languages** — a polyglot learning project.

One fixed API contract (a TodoMVC-style todo list), re-implemented across languages and frameworks to see what each layer of abstraction does, and hides. The low-level versions (C, C++) implement everything by hand — HTTP parsing, JSON, routing, memory. The higher-level ones (Node, Laravel) lean on their frameworks. Side by side, the differences are the lesson.

## The contract

Every implementation exposes the identical API:

| Method   | Path          | Description           | Success |
| -------- | ------------- | --------------------- | ------- |
| `GET`    | `/todos`      | List all todos        | `200`   |
| `POST`   | `/todos`      | Create a todo         | `201`   |
| `GET`    | `/todos/{id}` | Fetch a single todo   | `200`   |
| `PATCH`  | `/todos/{id}` | Partial update        | `200`   |
| `PUT`    | `/todos/{id}` | Full replace (strict) | `200`   |
| `DELETE` | `/todos/{id}` | Delete a todo         | `204`   |

Errors: `400` (malformed / invalid id), `404` (not found), `500` (server error).

### Todo schema

```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "title": "Buy milk",
  "completed": false
}
```

- `id` — a UUIDv4, assigned by the server
- `title` — string, required on create
- `completed` — boolean, defaults to `false`

`PATCH` is a partial update (send only what changes); `PUT` is a strict full replace (all fields required).

## Implementations

| Language / Stack    | Status         | Folder        |
| ------------------- | -------------- | ------------- |
| C (POSIX sockets)   | ✅ Done         | [`c/`](./c)   |
| C++                 | ⬜ Planned      | `cpp/`        |
| Rust                | ⬜ Planned      | `rust/`       |
| Go                  | ⬜ Planned      | `go/`         |
| Node (http)         | ⬜ Planned      | `node/`       |
| Express             | ⬜ Planned      | `express/`    |
| NestJS              | ⬜ Planned      | `nest/`       |
| PHP                 | ⬜ Planned      | `php/`        |
| Laravel             | ⬜ Planned      | `laravel/`    |

## Goal

Each version targets the same contract, so the interesting part is the *implementation*, not the feature set. Building the hardest version first (C, from raw sockets) makes it clear exactly what every framework automates — by the time the same API is a handful of lines in Express or Laravel, you know what those lines are doing underneath.

## Structure

```
todo-api/
├── README.md        # this file
├── c/               # the C implementation (done)
│   ├── main.c
│   ├── openapi.json
│   └── README.md
└── …                # one folder per language, added over time
```

Each language folder has its own README with build and run instructions.

## License

MIT

