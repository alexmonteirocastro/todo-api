
#include <cstdio>
#include <sys/socket.h> // socket, bind, listen, setsockopt
#include <netinet/in.h> // sockaddr_in, INADDR_ANY, AF_INET
#include <unistd.h> // close
#include "signals.h"
#include "store.h"
#include <cerrno>
#include <sstream>
#include "http.h"
#include "handlers.h"

/**
 * C++ way to write #define PORT 8080
 * Difference: #define is a blind text substitution the preprocessor does before compiling, 
 * while constexpr int is a real typed constant — the compiler and debugger know it’s an int named PORT. 
 * Same result, safer spelling.
 */
constexpr int PORT = 8080; 

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

    if (!store_init("todos.db")) {
        std::fprintf(stderr, "failed to open database \n");
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

   install_signal_handlers();
   
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