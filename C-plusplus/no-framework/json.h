#pragma once
// ============================================================================
// json.h — serializing todos out, and the naive JSON field readers in.
// ============================================================================
#include <string>
#include <optional>
#include "model.h"

/**
 * Serialize one TODO item and the whole store as a JSON array
 */
std::string todo_to_json(const Todo& t);
std::string todos_to_json();

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
std::optional<std::string> json_get_string(const std::string& body, const std::string& key);

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
BoolField json_get_bool(const std::string& body, const std::string& key);