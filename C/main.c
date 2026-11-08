#include <stdio.h>      // printf, perror
#include <stdlib.h>     // exit, arc4random_buf is here on macOS
#include <string.h>     // memset
#include <unistd.h>     // read, close
#include <sys/socket.h> // actual socket functions: socket(), bind(), listen(), accept(), setsockopt(). This is the real workhorse.
#include <netinet/in.h> // the IPv4 address structures and constants: struct sockaddr_in, INADDR_ANY, the AF_INET family
#include <arpa/inet.h>  // socket API, socaddr_in, htons, INADDR_ANY - mainly the byte-order conversion helpers like htons() (and address-string functions like inet_pton)
#include <ctype.h>      // for isxdigit
#include <signal.h> // for signal handling
#include <errno.h> // suppresses the spurious error message on shutdown

/*These are globals (declared at file scope)*/

#define PORT 8080
#define MAX_TODOS 100

volatile sig_atomic_t running = 1; // Shutdown flag - set to 0 by the signal handler to stop the main loop

// STATUS CODES
static const char *STATUS_200 = "200 OK";
static const char *STATUS_201 = "201 Created";
static const char *STATUS_204 = "204 No Content";
static const char *STATUS_400 = "400 Bad Request";
static const char *STATUS_404 = "404 Not Found";
static const char *STATUS_500 = "500 Internal Server Error";

// CONTENT TYPES
static const char *CONTENT_JSON = "application/json";
static const char *CONTENT_HTML = "text/html";

// Data Model + in-memory store
typedef struct
{
    char id[37];
    char title[256];
    int completed;
} Todo;

Todo todos[MAX_TODOS]; // the store
int todo_count = 0;


/**
 * Signal handler
 * Runs when SIGINT (Ctrl-C) arrives.
 */
void handle_sigint(int sig){
    (void)sig; // we don't use the signal number; silence the unused warning
    running = 0; // tell the main loop to stop
}

char *read_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if(f == NULL) return NULL;

    /**
     * This is a well-known C idiom for “how big is this file?” There’s a read position (a cursor) inside the open file, like a playhead, that moves as you read. 
     * We manipulate it to measure the size:
     * 
     * fseek(f, 0, SEEK_END) — “seek” (move the cursor) to 0 bytes from the end (SEEK_END). So now the cursor is at the very end of the file.
     * ftell(f) — “tell me the current cursor position.” Since the cursor is at the end, this position is the file’s size in bytes. 
     * We store it in size. (A long because files can be large — bigger than a regular int might hold.)
     * fseek(f, 0, SEEK_SET) — seek back to 0 bytes from the start (SEEK_SET). 
     * This rewinds the cursor to the beginning, so we’re ready to actually read from the start.
     */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size < 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc(size + 1);
    if(buf == NULL) {
        fclose(f);
        return NULL;
    }

    size_t read_n = fread(buf, 1, size, f);
    buf[read_n] = '\0';
    fclose(f);
    return buf;
}

/**
 * Send a complete HTTP response: status line + headers + body.
 * `status_line` is like "200 OK" or "404 Not Found".
 * `content_type` is like "text/plain" or "application/json".
 */
int send_response(int client_fd, const char *status_line,
                  const char *content_type, const char *body)
{
    // Build the headers (small, fixed - a plain stuck buffer shoudl suffice)
    char headers[256];
    int hlen = snprintf(headers, sizeof(headers),
                        "HTTP/1.1 %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "\r\n",
                        status_line,
                        content_type,
                        strlen(body));

    if (hlen < 0 || (size_t)hlen >= sizeof(headers))
    {
        return -1; // headers didn't fit or snprintf failed
    }

    // Send the headers first, then the body (two writes, same stream)
    write(client_fd, headers, hlen);
    write(client_fd, body, strlen(body));

    return 0; // sent OK
}

/**
 * Extracts a JSON string value by key, e.g. "title" from { "title": "h1"}
 * Writes the value into `out` (null-terminated, clamped to out_size)
 * Returns 1 if the key was found and parsed, 0 otherwise 
 */
int extract_json_to_string(const char *body, const char *key, char *out, size_t out_size) {
    // Build the search needle: "key" (with quotes)
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    char *k = strstr(body, needle);
    if(k == NULL) return 0;

    char *colon =strchr(k, ':');
    if(colon == NULL) return 0;

    char *start = strchr(colon, '"'); // opening quote of the value
    if(start == NULL) return 0;
    start += 1;

    char *end = strchr(start, '"'); // closing quote of the value
    if(end == NULL) return 0;

    size_t len = end - start;
    if(len >= out_size) len = out_size - 1; // leave space for '\0'
    memcpy(out, start, len); // copy exactly len bytes (now clamped)
    out[len] = '\0'; // null-terminate
    return 1;
}

/**
 * Extract a JSON boolean by key.
 * Writes 0 or 1 into *out.
 * Returns 1 if found and valid(true/false), 0 if absent, -1 if present but invalid.
 */
int extract_json_bool(const char *body, const char *key, int *out) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key); 

    char *k = strstr(body, needle);
    if(k == NULL) return 0; // absent

    char *colon =strchr(k, ':');
    if(colon == NULL) return -1; // malformed

    char *v = colon + 1; // step past the ':'
    while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;

    if(strncmp(v, "true", 4) == 0) {
        *out = 1;
        return 1;
    }
    if(strncmp(v, "false", 5) == 0) {
        *out = 0;
        return 1;
    }

    return -1; // present but not true/false
}

/**
 * Format a single TODO item as a JSON objecxt into `out`
 * Return the number of characters written (like snprintf)
 */
int todo_to_json(const Todo *t, char *out, size_t out_size)
{
    return snprintf(
        out,
        out_size,
        "{\"id\": \"%s\", \"title\": \"%s\", \"completed\":%s}",
        t->id,
        t->title,
        t->completed ? "true" : "false");
}

/**
 * Check that `s` is a well-formed UUID: 8-4-4-4-12 hex digits with hyphens
 * Returns 1 if valid, and 0 if not
 */
int is_valid_uuid(const char *s)
{
    // A UUID is exactly 36 characters - this validates the length
    if (strlen(s) != 36)
    {
        return 0;
    }

    for (int i = 0; i < 36; i++)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            // These four positions must be hyphens
            if (s[i] != '-')
                return 0;
        }
        else
        {
            /**
             * Every other position must be a hex digit
             *
             * isxdigit is a function from <ctype.h> (“character type”) that tests whether a character is a hexadecimal digit. It returns true (non-zero) for:
             * -> 0–9
             * -> a–f
             * -> A–F
             *
             * take character s[i], reinterpret it safely as a 0–255 byte, ask ‘is it a hex digit?’, and if not, the UUID is invalid.
             */
            if (!isxdigit((unsigned char)s[i]))
                return 0;
        }
    }

    return 1;
}

void generate_uuid(char *out)
{
    unsigned char bytes[16];              // 16 bytes, our raw UUID material. unsigned char because we want each to be 0–255 (a raw byte), not a signed -128–127.
    arc4random_buf(bytes, sizeof(bytes)); // fills buffer with 16 random bytes. Unlike the old rand(), it needs no srand() seeding and gives high-quality randomness.

    /**
     * The version line — bytes[6] = (bytes[6] & 0x0F) | 0x40;
     *
     * Byte 6 is where the version nibble lives (it maps to that 13th hex digit).
     * We need its high 4 bits (the high nibble) to be exactly 4 (binary 0100), while keeping its low 4 bits random.
     *
     * bytes[6] & 0x0F — the & (bitwise AND) with 0x0F (binary 00001111) clears the high nibble, keeps the low nibble.
     * AND-ing with 0 forces a bit to 0; AND-ing with 1 keeps it.
     * So 0x0F is a mask: zeros on top (clear those), ones on bottom (keep those). Result: high nibble = 0000, low nibble = still random.
     *
     * | 0x40 — the | (bitwise OR) with 0x40 (binary 01000000) sets the high nibble to 4.
     * OR-ing with 1 forces a bit to 1; OR-ing with 0 leaves it. 0x40’s high nibble is 0100 (=4), low nibble is 0000.
     * So this stamps 4 into the high nibble we just cleared, leaving the low nibble untouched.
     *
     * Net effect: high nibble forced to 4, low nibble stays random. That’s the “version 4” marker.
     *
     * The variant line — bytes[8] = (bytes[8] & 0x3F) | 0x80;
     *
     * Same idea, for the two variant bits (top 2 bits of byte 8):
     *
     * & 0x3F (00111111) — clears the top 2 bits, keeps the bottom 6.
     * | 0x80 (10000000) — sets the top 2 bits to 10.
     *
     * Result: top two bits forced to 10 (the standard variant), rest random. That’s what makes the 17th hex digit land in 8/9/a/b.
     *
     * The pattern (value & clear_mask) | set_mask is a classic bit-twiddling idiom: “clear these bits, then set those bits.”
     * We’ll see it everywhere in low-level code — hardware registers, protocols, flags. Worth internalizing.
     */
    bytes[6] = (bytes[6] & 0x0F) | 0x40; // version 4
    bytes[8] = (bytes[8] & 0x3F) | 0x80; // variant

    snprintf(
        out,
        37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0],
        bytes[1],
        bytes[2],
        bytes[3],
        bytes[4],
        bytes[5],
        bytes[6],
        bytes[7],
        bytes[8],
        bytes[9],
        bytes[10],
        bytes[11],
        bytes[12],
        bytes[13],
        bytes[14],
        bytes[15]);
}

/**
 * Find a TODO item by Id
 * Returns a pointer to it, or NULL if not found
 */
Todo *find_todo_by_id(const char *id)
{
    for (int i = 0; i < todo_count; i++)
    {
        if (strcmp(todos[i].id, id) == 0)
        {
            return &todos[i];
        }
    }
    return NULL;
}

int main(void)
{
    // 1. Create a TCP socket. AF_INET = IPV4, SOCK_STREAM = TCP.

    /**
     * --socket file descriptor (the OS’s ticket number for our listening socket)--
     * It asks the kernel: “create a communication endpoint and give me back a descriptor for it.”
     * The three arguments describe what kind of endpoint we want:
     * 1. domain → AF_INET — the address family, i.e. what kind of addresses this socket uses. AF_INET = IPv4 (addresses like 127.0.0.1).
     * 2. type → SOCK_STREAM — the communication style. SOCK_STREAM = a reliable, ordered byte stream = TCP. SOCK_DGRAM (“datagram”) would be UDP.
     * 3. protocol → 0 — the specific protocol within that family and type. 0 means “pick the default/obvious one for me.” For AF_INET + SOCK_STREAM, the only sensible
     * protocol is TCP, so 0 resolves to TCP automatically. We could write IPPROTO_TCP explicitly, but 0 is idiomatic and does the same thing here.
     * It only matters in rare cases where one (family, type) pair supports more than one protocol.
     *
     * Return value: on success, the new file descriptor (a non-negative int). On failure, -1.
     */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {                     // did socket() fail?
        perror("socket"); // print why
        exit(1);          // give up. Can't continue without a socket
    }

    // Lets us restart the server immediately without the "Address already in use".
    int opt = 1; // We put 1 (meaning “on”/true), then pass its address with &opt. Passing 0 would turn it off
    /**
     * setsockopt = “set socket option.” Sockets have many tunable options; this is the generic knob-setting function. Its five arguments:
     * server_fd — which socket to configure. The one we just created.
     * SOL_SOCKET — the level the option lives at. Options are grouped:
     *  SOL_SOCKET = generic socket-level options (as opposed to TCP-specific or IP-specific levels).
     *  SO_REUSEADDR is a general socket option, so it lives here.
     * SO_REUSEADDR — which option we’re setting. (“SO” = socket option.)
     * &opt — the value to set it to. Here’s a C quirk worth noting: this option is really a boolean (on/off),
     * but the API is generic and must accept options of any type, so it takes the value by pointer (void *) rather than by value.
     * sizeof(opt) — the size of that value in bytes.
     * Because optval is just a typeless pointer, the function has no idea how many bytes to read from it — so we must tell it.
     * sizeof(opt) is the size of our int (4 bytes).
     * This “pointer + length” pairing is a recurring C pattern for passing data of a type the callee doesn’t know in advance.
     */
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. Describe the address: any interface, port 8080

    struct sockaddr_in addr; // make an empty IPv4-address form
    /**
     * memset = “memory set.” It fills a block of memory with one repeated byte value.
     * &addr — where to start: the address of our struct. (& = “address of.”) memset works on raw memory, so it needs a pointer to the start.
     * 0 — what byte to write: zero. So every byte becomes 0.
     * sizeof(addr) — how many bytes: the full size of the struct. sizeof is computed at compile time, so this is always exactly the struct’s size, even if it changes.
     */
    memset(&addr, 0, sizeof(addr));    // zero it out first to wipe it clean (no garbage)
    addr.sin_family = AF_INET;         // it's an IPv4 address
    addr.sin_addr.s_addr = INADDR_ANY; // 0.0.0.0 - all interfaces
    addr.sin_port = htons(PORT);       // htons = host-to-network byte order - converts a 16-bit number (a “short”) from computer’s byte order to the network’s byte order

    // 3. Bind the socket to that address/port.

    /**
     * -> it runs bind(...) — the actual binding happens right here. bind then returns a value (0 on success, -1 on failure) <-
     * server_fd — which socket to bind. Our listening socket.
     * (struct sockaddr *)&addr — the address to bind it to: a pointer to the sockaddr_in we just filled in (all-interfaces, port 8080).
     * sizeof(addr) — how big that address struct is, since the function receives it through a generic pointer and needs to know how many bytes to read.
     */
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    { //  "treat this pointer as a generic-socket-address pointer"
        perror("bind");
        exit(1);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa)); // zero it
    sa.sa_handler = handle_sigint; // our handler function
    // NOTE: We deliberately do not set SA_RESTART, so accept() returns EINTR on signal
    sigaction(SIGINT, &sa, NULL);

    // 4. Start listening. 10 = how many connections may queue up.
    if (listen(server_fd, 10) < 0)
    {
        perror("listen");
        exit(1);
    }

    printf("Listening on http://localhost:%d\n", PORT);

    // 5. Accept connections one at a time, forever.
    while (running)
    {
        int client_fd = accept(server_fd, NULL, NULL); // Blocks until someone connects
        if (client_fd < 0)
        {
            if(errno == EINTR) continue; // interrupted by our signal - loop will exit via while(running)
            perror("accept");
            continue;
        }

        /**
         * char buffer[4096]; — a buffer: a chunk of memory (an array of 4096 chars, i.e. 4096 bytes) where we’ll stash the incoming request text.
         * 4096 is just a reasonable size — plenty for a simple HTTP request’s text.
         *
         * read(client_fd, buffer, sizeof(buffer) - 1) — read bytes from the client connection into buffer.
         * Three args: the socket to read from, where to put the bytes, and the max number to read.
         * Remember “everything is a file” — read is the same call we’d use on a disk file; here it pulls bytes off the network.
         * It also blocks until data arrives (or the client closes).
         *
         * Why sizeof(buffer) - 1 and not 4096? We deliberately read at most 4095 bytes, leaving one byte free at the end — reserved for the null terminator.
         * If we filled all 4096, there’d be no room for it. This small “−1” is a classic, important C habit for avoiding buffer overruns.
         *
         * ssize_t n — read returns how many bytes it actually read, stored in n.
         * (ssize_t is just “signed size type” — an integer that can also be -1 to signal an error; the extra s vs size_t is what lets it hold a negative.)
         * n could be fewer than we asked for — this is the “a stream isn’t tidy messages” point from TCP cc -Wall -Wextra -o server main.c
         * one read gives us whatever’s arrived so far, not guaranteed a whole request.
         */
        char buffer[4096];
        ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
        if (n > 0)
        { // only proceed if we actually read something
            /**
             * we write '\0' at index n — right after the last real byte.
             * Example: if read returned 52 bytes, they occupy buffer[0]…buffer[51], and we set buffer[52] = '\0'.
             * This is why we reserved that extra byte: index n is guaranteed to be within the array (worst case n is 4095, so buffer[4095] is valid).
             * Now buffer is a proper, printable C string.
             */
            buffer[n] = '\0'; // null-terminate so printf treats it as a string
            printf("---- raw  request ---\n%s\n---\n", buffer);

            // Parse the request line: "METHOD PATH VERSION"
            char method[8];
            char path[1024];
            if (sscanf(buffer, "%7s %1023s", method, path) != 2)
            {
                send_response(client_fd, STATUS_400, CONTENT_JSON, "{\"error\":\"Bad Request\"}");
                close(client_fd);
                continue;
            }

            // Routes
            if (strcmp(method, "GET") == 0 && strcmp(path, "/todos") == 0)
            {
                // GET /todos - list all TODO items (Starts empty)

                /**
                 * Each TODO item's JSON is, at most ~320 bytes; +2 for [], +1 per comma and +1 for \0
                 * Allocating generously based on the current count
                 */
                size_t capacity = todo_count * 320 + 64;
                char *todo_list = malloc(capacity);
                if (todo_list == NULL)
                {
                    send_response(client_fd, STATUS_500, CONTENT_JSON, "{\"error\":\"Out of memory\"}");
                    close(client_fd);
                    continue;
                }

                // Build the array [ todo, todo, ... ]
                size_t pos = 0;
                todo_list[pos++] = '[';

                for (int i = 0; i < todo_count; i++)
                {
                    if (i > 0)
                    {
                        todo_list[pos++] = ','; // comma between items, not before the first
                    }

                    // Append this TODO item/s JSON where we left off
                    pos += todo_to_json(&todos[i], todo_list + pos, capacity - pos);
                }

                todo_list[pos++] = ']';
                todo_list[pos] = '\0';

                send_response(client_fd, STATUS_200, CONTENT_JSON, todo_list);

                free(todo_list); // Free the allocated memory
            }
            else if (strcmp(method, "POST") == 0 && strcmp(path, "/todos") == 0)
            {
                // POST /todos - Create a new TODO item

                // 1. Are we full?
                if (todo_count >= MAX_TODOS)
                {
                    send_response(client_fd, STATUS_500, CONTENT_JSON, "{\"error\":\"TODO store is full\"}");
                    close(client_fd);
                    continue;
                }

                // 2. Find the request body (everything after the blank line)
                char *req_body = strstr(buffer, "\r\n\r\n");
                if (req_body == NULL)
                {
                    send_response(client_fd, STATUS_400, CONTENT_JSON, "{\"error\":\"Bad Request\"}");
                    close(client_fd);
                    continue;
                }
                req_body += 4; // skip past "\r\n\r\n"

                /**
                 * 3. Extract the "title" value from the JSON body
                 * We look for "title" then the : then the oprning " of the value
                 */
                char title[256];
                if(!extract_json_to_string(req_body, "title", title, sizeof(title))) {
                    send_response(client_fd, STATUS_400, CONTENT_JSON, "{\"error\": \"Title is required\"}");
                    close(client_fd);
                    continue;
                }

                // 5. Create the TODO item in our store
                Todo *t = &todos[todo_count]; // point at the next free slot
                generate_uuid(t->id);         // assign id
                strcpy(t->title, title);      // copy the title in
                t->completed = 0;             // default: not completed
                todo_count++;                 // one more todo exists

                // 6. Respond 201 with the full created todo
                char response_body[512];
                todo_to_json(t, response_body, sizeof(response_body));
                send_response(client_fd, STATUS_201, CONTENT_JSON, response_body);
            }
            else if (strcasecmp(method, "GET") == 0 && strncasecmp(path, "/todos/", 7) == 0)
            {
                // GET /todos/:id - fetch a single TODO item

                // 1. Extract the id: everything after /todos
                char *id = path + 7;

                // 2. Validate if it's a real UUID
                if (!is_valid_uuid(id))
                {
                    send_response(client_fd, STATUS_400, CONTENT_JSON, "{\"error\":\"Invalid id\"}");
                    close(client_fd);
                    continue;
                }

                // 3. Find the TODO item with that id
                Todo *found = find_todo_by_id(id);

                // 4. Not found -> 404
                if (found == NULL)
                {
                    send_response(client_fd, STATUS_404, CONTENT_JSON, "{\"error\": \"Item not found\"}");
                    close(client_fd);
                    continue;
                }

                // 5. Found -> 200 with the TODO item as JSON
                char response_body[512];
                todo_to_json(found, response_body, sizeof(response_body));
                send_response(client_fd, STATUS_200, CONTENT_JSON, response_body);
            }
            else if (strcasecmp(method, "PATCH") == 0 && strncasecmp(path, "/todos/", 7) == 0)
            {
                // Patch /todos/:id - partial update on a TODO item

                // 1. Extract the id: everything after /todos
                char *id = path + 7;

                // 2. Validate if it's a real UUID
                if (!is_valid_uuid(id))
                {
                    send_response(client_fd, STATUS_400, CONTENT_JSON, "{\"error\":\"Invalid id\"}");
                    close(client_fd);
                    continue;
                }

                // 3. Find the TODO item with that id
                Todo *found = find_todo_by_id(id);

                // 4. Not found -> 404
                if (found == NULL)
                {
                    send_response(client_fd, STATUS_404, CONTENT_JSON, "{\"error\": \"Item not found\"}");
                    close(client_fd);
                    continue;
                }

                // 2. Find the request body (everything after the blank line)
                char *req_body = strstr(buffer, "\r\n\r\n");
                if (req_body == NULL)
                {
                    send_response(client_fd, STATUS_400, CONTENT_JSON, "{\"error\":\"Bad Request\"}");
                    close(client_fd);
                    continue;
                }
                req_body += 4; // skip past "\r\n\r\n"

                // Update title if present
                char new_title[256];
                if(extract_json_to_string(req_body, "title", new_title, sizeof(new_title))) strcpy(found->title, new_title);

                // Update completed if present (tolerate whitespace after the colon)
                int completed;
                int r = extract_json_bool(req_body, "completed", &completed);
                if(r == 1) found->completed = completed; // only update if present and valid
                else if (r == -1) {
                    send_response(client_fd, STATUS_400, CONTENT_JSON, "{\"error\": \"Completed must be true or false\"}");
                    close(client_fd);
                    continue;
                }
                // if "completed" is absent (r == 0) -> left unchanged

                // Respond 200 with the updated TODO item
                char response_body[512];
                todo_to_json(found, response_body, sizeof(response_body));
                send_response(client_fd, STATUS_200, CONTENT_JSON, response_body);
            }
            else if (strcasecmp(method, "PUT") == 0 && strncasecmp(path, "/todos/", 7) == 0)
            {
                // PUT /todos/:id - full replace

                // 1. Extract the id: everything after /todos
                char *id = path + 7;

                // 2. Validate if it's a real UUID
                if (!is_valid_uuid(id))
                {
                    send_response(client_fd, STATUS_400, CONTENT_JSON, "{\"error\":\"Invalid id\"}");
                    close(client_fd);
                    continue;
                }

                // 3. Find the TODO item with that id
                Todo *found = find_todo_by_id(id);

                // 4. Not found -> 404
                if (found == NULL)
                {
                    send_response(client_fd, STATUS_404, CONTENT_JSON, "{\"error\": \"Item not found\"}");
                    close(client_fd);
                    continue;
                }

                // 5. Find the request body (everything after the blank line)
                char *req_body = strstr(buffer, "\r\n\r\n");
                if (req_body == NULL)
                {
                    send_response(client_fd, STATUS_400, CONTENT_JSON, "{\"error\":\"Bad Request\"}");
                    close(client_fd);
                    continue;
                }
                req_body += 4; // skip past "\r\n\r\n"

                // title is required!
                char new_title[256];
                if(!extract_json_to_string(req_body, "title", new_title, sizeof(new_title))) {
                    send_response(client_fd, STATUS_400, CONTENT_JSON, "{\"error\": \"Title is malformed\"}");
                    close(client_fd);
                    continue;
                }
                strcpy(found->title, new_title);

                // Completed is also required
                int completed;
                int r = extract_json_bool(req_body, "completed", &completed);
                if(r != 1) {
                    send_response(client_fd, STATUS_400, CONTENT_JSON, "{\"error\": \"Completed is required and must be true or false\"}");
                    close(client_fd);
                    continue;
                }
                

                // Respond 200 with the updated TODO item
                char response_body[512];
                todo_to_json(found, response_body, sizeof(response_body));
                send_response(client_fd, STATUS_200, CONTENT_JSON, response_body);
            }
            else if(strcasecmp(method, "DELETE") == 0 && strncasecmp(path, "/todos/", 7) == 0) 
            {
                // DELETE /todos/:id - remove a TODO item

                // 1. Extract the id: everything after /todos
                char *id = path + 7;

                // 2. Validate if it's a real UUID
                if (!is_valid_uuid(id))
                {
                    send_response(client_fd, STATUS_400, CONTENT_JSON, "{\"error\":\"Invalid id\"}");
                    close(client_fd);
                    continue;
                }

                // 3. Find the TODO item with that id
                Todo *found = find_todo_by_id(id);

                // 4. Not found -> 404
                if (found == NULL)
                {
                    send_response(client_fd, STATUS_404, CONTENT_JSON, "{\"error\": \"Item not found\"}");
                    close(client_fd);
                    continue;
                }

                int index = found - todos; // how many elements from the start → the index
                // Remove item: shift everything after it down one slot
                for(int i = index; i < todo_count; i++) {
                    todos[i] = todos[i+1]; // Copy the next TODO item into this slot
                }
                todo_count--; // one fewer todo now;

                // Respond 204 No Content (success, nothing to return)
                send_response(client_fd, STATUS_204, CONTENT_JSON, "");


            }
            else if(strcasecmp(method, "GET") == 0 && strcmp(path, "/openapi.json") == 0)  
            {
                char *spec = read_file("openapi.json");
                if(spec == NULL) {
                    send_response(client_fd, STATUS_500, CONTENT_JSON, "{\"error\": \"Could not load spec\"}");
                    close(client_fd);
                    continue;
                }

                send_response(client_fd, STATUS_200, CONTENT_JSON, spec);
                free(spec);
            }
            else if(strcasecmp(method, "GET") == 0 && strcmp(path, "/docs") == 0)  
            {
                const char *docs_page = 
                    "<!DOCTYPE html><html><head>"
                    "<title>Todo API Docs</title>"
                    "<link rel=\"stylesheet\" href=\"https://unpkg.com/swagger-ui-dist/swagger-ui.css\">"
                    "</head><body>"
                    "<div id=\"swagger-ui\"></div>"
                    "<script src=\"https://unpkg.com/swagger-ui-dist/swagger-ui-bundle.js\"></script>"
                    "<script>"
                    "SwaggerUIBundle({ url: '/openapi.json', dom_id: '#swagger-ui' });"
                    "</script></body></html>";
                send_response(client_fd, STATUS_200, CONTENT_HTML, docs_page);
            }
            else
            {
                send_response(client_fd, STATUS_404, CONTENT_JSON, "{\"error\":\"Not Found\"}");
            }
        }
        close(client_fd); // hang up on this client
    }
    
    printf("\nShutting down gracefully...\n");
    close(server_fd);

    return 0;
}
