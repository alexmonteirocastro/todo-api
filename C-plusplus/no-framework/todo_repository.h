#pragma once
#include <string>
#include <vector>
#include <optional>
#include "model.h"

/**
 * Anything that can store TODO items
 * Says what it can do, not how
 * 
 * class and public: Members of a struct are public by default, and members of a class are private by default.
 * public: makes everything below it usable from outside. 
 * By convention, struct is for plain data like Todo, and class is for things with behaviour.
 * 
 * virtual. This marks a function that subclasses can replace with their own version. 
 * When a handler calls repo.get(id), C++ checks at runtime which actual object it’s holding (SQLite or in-memory) and calls that one’s get. 
 * That runtime choice is what lets we swap implementations.
 * 
 * = 0. “Pure virtual”: this function has no body here, and every subclass must provide one. 
 * A class with any = 0 function is abstract, so we can’t create one directly
 * 
 * virtual ~TodoItems() = default;. 
 * A virtual destructor. 
 * Any class meant to be used through a base-class pointer or reference needs one. 
 * Without it, destroying a SqliteTodoRepository through a TodoItems* would skip the SQLite part’s cleanup (like closing the database). = default means “the normal, empty destructor.” Rule of thumb: if a class has any virtual function, give it a virtual destructo
 */
class TodoItems {
public:
    virtual std::vector<Todo>    all() = 0;
    virtual std::optional<Todo>  get(const std::string& id) = 0; 
    virtual bool add(const Todo& t) = 0;
    virtual bool update(const Todo& t) = 0;
    virtual bool remove(const std::string& id) = 0;

    virtual ~TodoItems() = default;
};
