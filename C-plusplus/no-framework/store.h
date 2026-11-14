#pragma once
// ============================================================================
// store.h — the in-memory todo store. (SQLite will replace the guts here later.)
// ============================================================================
#include <string>
#include <vector>
#include "model.h"

/**
 * `extern` = "this variable EXISTS somewhere — don't create it here, just announce it."
 *  Every file that includes store.h sees this announcement; the ONE real definition
 *  lives in store.cpp. Without extern, each file would make its own `todos` and the
 *  linker would complain about multiple definitions.
 */
extern std::vector<Todo> todos; 

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
Todo* find_todo_item(const std::string& id);