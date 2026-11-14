#include "routes.h"
// The route handlers pull together every other module
#include "http.h" // Status, send_response, read_file, content types
#include "json.h" // todo_to_json, todos_to_json, json_get_string, json_get_bool
#include "uuid.h" // generate_uuid, is_valid_uuid
#include "store.h" // todos, find_todo_item
#include "model.h" // Todo, BoolField

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