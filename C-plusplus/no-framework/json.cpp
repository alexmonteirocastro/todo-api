#include "json.h"
#include "store.h" // todos_to_json iterates the global `todos`
#include <format>

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

std::optional<std::string> json_get_string(const std::string& body, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t k = body.find(needle);             if(k == std::string::npos)     return std::nullopt;
    size_t colon = body.find(':', k);         if(colon == std::string::npos) return std::nullopt;
    size_t open = body.find('"', colon);      if(open == std::string::npos)  return std::nullopt;
    size_t close = body.find('"', open + 1);  if(close == std::string::npos) return std::nullopt;
    return body.substr(open + 1, close - (open + 1));
}

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