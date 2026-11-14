#include "store.h" // a .cpp always includes its own header first

// The ONE real definition of the store. store.h only announced it (extern).
std::vector<Todo> todos; // The in memory store. Starts empty


Todo* find_todo_item(const std::string& id) {
    for (auto& t : todos) { // reference loop -- walk each TODO item in store
        if(t.id == id) return &t; // match? hand back its ADDRESS
    }
    return nullptr; // walked them all, no match
}