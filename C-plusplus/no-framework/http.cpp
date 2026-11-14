#include "http.h"
#include <format>
#include <fstream>
#include <sstream>
#include <unistd.h> // write

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

void send_response(int fd, const Status status,
                   const char* type, const std::string& body) {
    std::string resp = std::format(
                        "HTTP/1.1 {}\r\nContent-Type: {}\r\nContent-Length: {} \r\n\r\n{}",
                        status_line(status), type, body.size(), body
                       );
    
    write(fd, resp.c_str(), resp.size());
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary); // open then file
    if(!f) return {}; // couldn't open -> empty string (signal failure)
    std::ostringstream ss; // a string builder stream
    ss << f.rdbuf(); // Slurp the whole file into it
    return ss.str(); // hand back the contents
}