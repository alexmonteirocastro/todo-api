
#include <cstdio>
#include <string>
#include <sys/socket.h> // socket, bind, listen, setsockopt
#include <netinet/in.h> // sockaddr_in, INADDR_ANY, AF_INET
#include <unistd.h> // close
#include <csignal>
#include <cerrno>
#include <sstream>
#include <vector>
#include <format>
#include <optional>
#include <random>
#include <array>
#include <cctype> // std::isxdigit
#include <fstream>

/**
 * C++ way to write #define PORT 8080
 * Difference: #define is a blind text substitution the preprocessor does before compiling, 
 * while constexpr int is a real typed constant — the compiler and debugger know it’s an int named PORT. 
 * Same result, safer spelling.
 */
constexpr int PORT = 8080; 

/**
 * the handler can fire at any instant, interrupting main mid-line. 
 * `sig_atomic_t` is a type guaranteed to be read/written in one uninterruptible step (no half-updated value), 
 * and `volatile` forbids the compiler from caching running in a register — it must re-read the real variable each loop, 
 * since it can change “from outside.” 
 * It’s the only kind of variable you may safely touch from a signal handler.
 */
volatile sig_atomic_t running = 1; // the shutdown flag
void handle_sigint(int) {
    running = 0; // Ctrl-C flips it to zero
}

enum class Status {
    Ok          = 200,
    Created     = 201,
    NoContent   = 204,
    BadRequest  = 400,
    NotFound    = 404,
    ServerError = 500
};

std::string status_line(Status s) {
    switch (s) {
        case Status::Ok:           return "200 OK";
        case Status::Created:      return "201 Created";
        case Status::NoContent:    return "204 No Content";
        case Status::BadRequest:   return "400 Bad Request";
        case Status::NotFound:     return "404 Not Found";
        case Status::ServerError:  return "500 Internal Server Error";
    }
    return "500 Internal Server Error"; // unreachable, keeps the compiler happy
}

constexpr const char* CONTENT_JSON = "application/json";
constexpr const char* CONTENT_HTML = "text/html";

struct Todo {
    std::string id;
    std::string title;
    bool completed = false;
};

std::vector<Todo> todos; // The in memory store. Starts empty

struct BoolField {
    bool present; // is the key there at all?
    bool valid;   // if present, is it actually true/false
    bool value;   // the parsed value (only meaningful when present and valid)
};

/**
 * Serialize one TODO item and the whole store as a JSON array
 */
std::string todo_to_json(const Todo& t) {
    return "{\"id\": \"" + t.id + "\", \"title\": \"" + t.title + "\", \"completed\": " + (t.completed ? "true" : "false") + "}";
}

std::string todos_to_json() {
    std::string out = "[";
    for (size_t i = 0; i < todos.size(); ++i) { // emoty store -> loop never runs -> []
        if (i > 0) out += ",";
        out += todo_to_json(todos[i]);
    }
    out += "]";
    return out;
}

void send_response(int fd, const Status status,
                   const char* type, const std::string& body) {
    std::string resp = std::format(
                        "HTTP/1.1 {}\r\nContent-Type: {}\r\nContent-Length: {} \r\n\r\n{}",
                        status_line(status), type, body.size(), body
                       );
    
    write(fd, resp.c_str(), resp.size());
}

/**
 * pull title out of the JSON body, returning std::optional
 * 
 * std::optional<std::string> means “either a string, or nothing.” 
 * 
 * std::string::npos is the value .find() returns when it doesn’t find what we asked for. 
 * It’s the “not found” sentinel.
 * Mechanically, .find() returns a size_t — the index where the match starts (0, 1, 2, …).
 * But it needs some value to mean “no match,” and 0 is already a valid index (the first character). 
 * So the designers picked the largest possible size_t — a huge number like 18446744073709551615 
 * — and named it npos (“no position”).
 * `if (k == std::string::npos)`   -> "the needle wasn't found"
 * 
 * We return std::nullopt for “absent” and the value for “found.” 
 * The caller checks if (!title) and reads it with *title — the “might be missing” lives in the type instead of a separate return flag.
 * 
 * The return line
 * substr(start, length) extracts a substring. Two arguments:
 *  open + 1 = the start. open points at the opening quote, so open + 1 is the first real character of the value (B).
 * close - (open + 1) = the length. It’s “the position just after the value” minus “where the value starts” = the number of characters between the quotes.
 */
std::optional<std::string> json_get_string(const std::string& body, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t k = body.find(needle);             if(k == std::string::npos)     return std::nullopt;
    size_t colon = body.find(':', k);         if(colon == std::string::npos) return std::nullopt;
    size_t open = body.find('"', colon);      if(open == std::string::npos)  return std::nullopt;
    size_t close = body.find('"', open + 1);  if(close == std::string::npos) return std::nullopt;
    return body.substr(open + 1, close - (open + 1));
}

/**
 * `std::random_device rd;` — a source of real entropy from the OS (hardware randomness, /dev/urandom, etc.). 
 * It’s high-quality but slow, so we don’t use it for every number — you use it once, to seed the fast generator.
 * std::mt19937 gen(rd());` — the actual engine: a Mersenne Twister (the 19937 is its period, 2¹⁹⁹³⁷−1 — absurdly long before it repeats). 
 * It’s a pseudo-random generator: fast, deterministic given a seed. gen(rd()) seeds it by calling rd() once. 
 * Think of rd as striking the match, gen as the fire that burns fast after.
 * `std::uniform_int_distribution<int> dist(0, 255);` — the shaper. 
 * The engine spits out raw bits across its full range; 
 * the distribution maps those onto the range we actually want — here, 0 to 255 (one byte), uniformly (every value equally likely, no modulo bias). 
 * We use it as dist(gen) — “give me a shaped number, pulling raw bits from gen.”
 * 
 * Why the static on the first two? 
 * static inside a function means “create this once, on the first call, and keep it alive for the whole program.”
 *  Without it, every call to generate_uuid() would build a fresh random_device and re-seed a fresh engine — slow, 
 * and worse, two calls in the same instant could seed identically and produce the same UUID. 
 * static means the engine is created and seeded once, then just keeps advancing. 
 * (The dist is cheap and stateless, so it doesn’t need static — though it wouldn’t hurt.)
 */
std::string generate_uuid() {
    static std::random_device rd;                     // 1. the seed source
    static std::mt19937 gen(rd());                    // 2. the generator engine
    std::uniform_int_distribution<int> dist(0, 255);  // 3. the shaper

    std::array<unsigned char, 16> b{};
    // auto = “let the compiler figure out the type” (it’s unsigned char here).
    // fill this slot with a fresh random 0–255 value. dist(gen) produces an int;
    for (auto& byte : b) byte = static_cast<unsigned char>(dist(gen));
    b[6] = (b[6] & 0x0F) | 0x40; // version 4
    b[8] = (b[8] & 0x3F) | 0x80; // version 10xx
    
    char out[37]; // a plain buffer. 32 hex digits + 4 hyphens + 1 null terminator = 37.
    std::snprintf(out, sizeof(out), 
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]
    );

    return std::string(out); // wrap the C char[] back into a std::string to return. (This is the reverse of .c_str())
}

bool is_valid_uuid (const std::string& s) {
    if (s.size() != 36) return false; // a UUID is always 36 chars long!

    for (size_t i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false; // them four spots must be hyphens
        } else {
            if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false; // all other charcaters must be hex
        }
    }

    return true;
}

/**
 * Why return Todo* (a pointer), not Todo (a copy)? 
 * Because the caller is going to change the todo — found->title = .... 
 * If we returned a copy, the caller would edit a throwaway and the real one in the store would be untouched. 
 * A pointer points at the actual element inside todos, so writing through it changes the stored todo. 
 * And a pointer gives us a natural “not found” value: nullptr. 
 * This is the direct parallel to our C’s find_todo_by_id returning Todo* / NULL.
 * 
 * The & appears twice, doing two different jobs — worth separating them:
 * 
 * for (auto& t : todos) — the & makes t a reference to each element, so we’re looking at the real todos, not copies. 
 * (Not strictly required just to read t.id, but it avoids copying a whole Todo each iteration, 
 * and it’s needed so that &t on the next line is the address of the real element.)
 * 
 * return &t; — here & means “address of” — give me a pointer to this element. 
 * That’s the pointer we hand back.
 */
Todo* find_todo_item(const std::string& id) {
    for (auto& t : todos) { // reference loop -- walk each TODO item in store
        if(t.id == id) return &t; // match? hand back its ADDRESS
    }
    return nullptr; // walked them all, no match
}

/**
 * The whitespace skip. A string value starts right after an opening " —
 * easy to find. A bool has no quotes: {"completed": true} has a space after
 * the colon. So we step past any spaces/tabs/newlines to land on the first
 * real character of the value. (json_get_string dodged this because it
 * searched for the " directly.)
 *
 * body.compare(v, 4, "true") asks "do the 4 characters starting at index v
 * equal 'true'?" It returns 0 when they match (same convention as C's strcmp
 * — 0 means equal), so we test the value without copying anything out.
 * "false" is 5 characters, hence the 5.
 *
 *   body contains            -> {present, valid, value}
 *   ----------------------------------------------------
 *   (no "completed" key)     -> {false, false, false}
 *   "completed": true        -> {true,  true,  true}
 *   "completed": false       -> {true,  true,  false}
 *   "completed": "yes"       -> {true,  false, false}
 */
BoolField json_get_bool(const std::string& body, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t k = body.find(needle);
    if (k == std::string::npos) return {false, false, false}; // ABSENT

    size_t colon = body.find(':', k);
    if (colon == std::string::npos) return {true, false, false}; // present, but malformed

    // skip whitespace after the colon
    size_t v = colon + 1;
    while (v < body.size() && (body[v] ==' ' || body[v] == '\t' || body[v] == '\r')) ++v;

    if (body.compare(v, 4, "true") == 0) return {true, true, true}; // present, value is `true`
    if (body.compare(v, 5, "false") == 0) return {true, true, false}; // present, value is `false`
    
    return {true, false, false}; // present, but not a boolean -> garbage
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary); // open then file
    if(!f) return {}; // couldn't open -> empty string (signal failure)
    std::ostringstream ss; // a string builder stream
    ss << f.rdbuf(); // Slurp the whole file into it
    return ss.str(); // hand back the contents
}

/** HANDLERS FOR THE ROUTES **/
void handle_get_all(int fd) {
    send_response(fd, Status::Ok, CONTENT_JSON, todos_to_json());
}

void handle_post(int fd, const std::string& body) {
    auto title = json_get_string(body, "title");
    if(!title) {
        send_response(fd, Status::BadRequest, CONTENT_JSON, "{\"error\": \"Title is required\"}");
        return;
    }
    Todo new_todo;
    new_todo.id = generate_uuid();
    new_todo.title = *title;
    new_todo.completed = false;
    todos.push_back(new_todo); // store grows by one item
    // Sends the latest created TODO item
    send_response(fd, Status::Created, CONTENT_JSON, todo_to_json(todos.back()));
}

void handle_get_one(int fd, const std::string& id) {
    //. Validate the shape
    if (!is_valid_uuid(id)) {
        send_response(fd, Status::BadRequest, CONTENT_JSON, "{\"error\": \"Invalid id\"}");
        return;
    } 
        
    // Look it up
    Todo* found = find_todo_item(id);
    if (!found) {
            send_response(fd, Status::NotFound, CONTENT_JSON, "{\"error\": \"Item not Found\"}");
            return;
    }

    // happy path 
    send_response(fd, Status::Ok, CONTENT_JSON, todo_to_json(*found));
}

void handle_put(int fd, const std::string& id, const std::string& body) {
    //. Validate the shape
    if (!is_valid_uuid(id)) {
        send_response(fd, Status::BadRequest, CONTENT_JSON, "{\"error\": \"Invalid id\"}");
        return;
    } 
        
    // Look it up
    Todo* found = find_todo_item(id);
    if (!found) {
            send_response(fd, Status::NotFound, CONTENT_JSON, "{\"error\": \"Item not Found\"}");
            return;
    }

    // Title is REQUIRED
    auto title = json_get_string(body, "title");
    if(!title) {
        send_response(fd, Status::BadRequest, CONTENT_JSON, "{\"error\": \"title is required\"}");
        return;
    } 
                    
    // Completed is REQUIRED
    BoolField completed = json_get_bool(body, "completed");
    if (!completed.present || !completed.valid) {
        send_response(fd, Status::BadRequest, CONTENT_JSON, "{\"error\": \"completed is required and must be true or false\"}");
        return;
    } 

    // Replace both fields, respond 200
    found->title = *title;
    found->completed = completed.value;
    send_response(fd, Status::Ok, CONTENT_JSON, todo_to_json(*found));
}

void handle_patch(int fd, const std::string& id, const std::string& body) {
    //. Validate the shape
    if (!is_valid_uuid(id)) {
        send_response(fd, Status::BadRequest, CONTENT_JSON, "{\"error\": \"Invalid id\"}");
        return;
    } 
        
    // Look it up
    Todo* found = find_todo_item(id);
    if (!found) {
            send_response(fd, Status::NotFound, CONTENT_JSON, "{\"error\": \"Item not Found\"}");
            return;
    }

    // title: Update ONLY if present
    auto title = json_get_string(body, "title");
    if(title) found->title = *title;

    // completed - update ONLY if present (but validate)
    BoolField completed = json_get_bool(body, "completed");
    if (completed.present && !completed.valid) {
        send_response(fd, Status::BadRequest, CONTENT_JSON, "{\"error\": \"completed must be true or false\"}");
        return;
    }
    if (completed.present) found->completed = completed.value;

    send_response(fd, Status::Ok, CONTENT_JSON, todo_to_json(*found));
}

void handle_delete(int fd, const std::string& id) {
    //. Validate the shape
    if (!is_valid_uuid(id)) {
        send_response(fd, Status::BadRequest, CONTENT_JSON, "{\"error\": \"Invalid id\"}");
        return;
    } 
        
    // Look it up
    Todo* found = find_todo_item(id);
    if (!found) {
            send_response(fd, Status::NotFound, CONTENT_JSON, "{\"error\": \"Item not Found\"}");
            return;
    }

    // erase it, convert the pointer to an iterator by its offset from the start
    todos.erase(todos.begin() + (found - todos.data()));

    // 204 No Content - Success, nothing to return
    send_response(fd, Status::NoContent, CONTENT_JSON, "");
}

void handle_open_api(int fd) {
    std::string spec = read_file("openapi.json");
    if (spec.empty()) {
        send_response(fd, Status::ServerError, CONTENT_JSON, "{\"error\": \"Could not load spec\"}");
        return;
    }
    send_response(fd, Status::Ok, CONTENT_JSON, spec);
}

void handle_docs(int fd) {
    const char* page = 
        "<!DOCTYPE html>"
        "<html>"
            "<head>"
                "<title>TODO API Docs</title>"
                "<link rel=\"stylesheet\" href=\"https://unpkg.com/swagger-ui-dist/swagger-ui.css\">"
            "</head>"
            "<body>"
                "<div id=\"swagger-ui\"></div>"
                "<script src=\"https://unpkg.com/swagger-ui-dist/swagger-ui-bundle.js\"></script>"
                "<script>SwaggerUIBundle({ url: 'openapi.json', dom_id: '#swagger-ui' });</script>"
            "</body>"
        "</html>";
    send_response(fd, Status::Ok, CONTENT_HTML, page);
}


int main() {
    /**
     * 1. Create a TCP socket over IPv4
     * 
     * Three arguments describe what kind of endpoint we want:
     * 
     * AF_INET = the IPv4 address family (addresses like 127.0.0.1).
     * SOCK_STREAM = a reliable, ordered byte stream = TCP. (SOCK_DGRAM would be UDP.)
     * 0 = “pick the default protocol for that combination,” which is TCP.
     * 
     * It returns a file descriptor — the OS’s ticket number for this socket.
     */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::perror("socket");
        return 1; 
    }

    /**
     * (Optional) allow instant restart without "Address already in use"
     */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /**
     * 2. Describe the address: IPv4, any network interface, port 8080
     * This struct says where to listen:
     * 
     * `sin_family = AF_INET` — IPv4 again (the struct has to agree with the socket).
     * `sin_addr.s_addr = INADDR_ANY` — this is 0.0.0.0, meaning “any interface.” 
     * Our machine may have several addresses (localhost, your Wi-Fi IP, etc.); 
     * `INADDR_ANY` says “accept connections arriving on any of them.” 
     * If we instead bound to 127.0.0.1 only, we’d accept local connections but reject ones from other machines.
     * `sin_port = htons(PORT)` — the port, 8080. 
     * The htons is the one genuinely non-obvious bit: it’s host-to-network-short, 
     * converting the number from our CPU’s byte order into the network’s standard byte order (networks always use big-endian). 
     * Skip it and a little-endian machine would advertise port 36895 instead of 8080. 
     * It’s a mechanical “speak the network’s dialect” step.
     */
    sockaddr_in addr{};                 // {} zero fills it (C used memset)
    addr.sin_family = AF_INET;          // IPv4
    addr.sin_addr.s_addr = INADDR_ANY;  // 0.0.0.0 - listen on every interface
    addr.sin_port = htons(PORT);        // port 8080, in network byte order

    /* 3. Bind the socket to that address/port */
    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::perror("bind");
        return 1;
    }

    /* 4. Start listening (10 = how many pending connections may queue)*/
    if (listen(server_fd, 10) < 0) {
        std::perror("listen");
        return 1;
    }

    std::printf("Listening on http://localhost:%d\n", PORT);

    struct sigaction sa{};
    sa.sa_handler = handle_sigint;
    /**
     * By default, many systems auto-resume a syscall that a signal interrupts (that’s SA_RESTART), 
     * which would mean accept just goes back to sleep and never notices running changed — our server would ignore Ctrl-C. 
     * By not setting that flag, the signal makes accept() return -1 with errno == EINTR, 
     * which breaks us out so the while (running) check can see the flag and exit.
     */
    sigaction(SIGINT, &sa, nullptr); // NOTE: no SA_RESTART on purpose

    /**
     * 5. Accept connections, one at a time, forever
     */
    while (running) {
        int client_fd = accept(server_fd, nullptr, nullptr); // blocks until a client connects
        if (client_fd < 0) {
            if (errno == EINTR) continue; // accept was interrupted by Ctrl-C -> re-check running
            std::perror("accept");
            continue;
        }
        
        /**
         * 1. read the raw request bytes
         */
        char buf[4096];
        ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
        if(n <= 0) {
            close(client_fd);
            continue;
        }
        buf[n] = '\0';
        std::string request(buf, n); // bytes -> std::string

        /**
         * 2. Parse the request line "METHOD PATH VERSION"
         */
        std::istringstream iss(request); // sscanf tidy C++ cousin
        std::string method, path, version;
        iss >> method >> path >> version; // splits on whitespace

        std::printf("parsed -> method=%s path=%s\n", method.c_str(), path.c_str()); 

        // Body, extracted once
        std::string body;
        size_t sep = request.find("\r\n\r\n");
        if(sep != std::string::npos) body = request.substr(sep + 4); // +4 skips the \r\n\r\n

        /**
         * 3. Routing
         */
        if (method == "GET" && path == "/todos")       handle_get_all(client_fd);
        else if (method == "GET" && path.compare(0, 7, "/todos/") == 0) handle_get_one(client_fd, path.substr(7));
        else if (method == "POST" && path == "/todos") handle_post(client_fd, body);
        else if (method == "PUT" && path.compare(0, 7, "/todos/") == 0) handle_put(client_fd, path.substr(7), body);
        else if (method == "PATCH" && path.compare(0, 7, "/todos/") == 0) handle_patch(client_fd, path.substr(7), body);
        else if (method == "DELETE" && path.compare(0, 7, "/todos/") == 0) handle_delete(client_fd, path.substr(7));
        else if (method == "GET" && path == "/openapi.json") handle_open_api(client_fd);
        else if (method == "GET" && path == "/docs") handle_docs(client_fd);
        else { 
            send_response(client_fd, Status::NotFound, CONTENT_JSON, "{\"error\": \"Not Found\"}");
        }

        close(client_fd); // hang up on THIS client. Kepp listening...
    }
   
    std::printf("Shutting down gracefully...\n");
    close(server_fd);
    return 0;
}