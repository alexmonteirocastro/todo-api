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

/** Open the DB file and ensure the table exists */
bool store_init(const std::string& path);

/** Closign the DB connection */
void store_close();

/* Every todo (maybe empty)*/
std::vector<Todo> store_all();

/*One TODO item, or nothing*/
std::optional<Todo> store_get(const std::string& id);

/** 
 * Insert a TODO item 
 * true on success
 * */
bool store_add(const Todo& t);

/**
 * Save changes to an existing TODO item
 * false if no such id
 */
bool store_update(const Todo& t);

/**
 * Delete by id
 * false if no such id
 */
bool store_remove(const std::string& id);