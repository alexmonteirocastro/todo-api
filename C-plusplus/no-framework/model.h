#pragma once
// ============================================================================
// model.h — the plain data types, shared everywhere.
// Header-only: structs are just declarations, so there's no model.cpp to compile.
// #pragma once = "paste this file at most once per compile" — stops the struct
// being defined twice when several files #include it.
// ============================================================================
#include <string>

struct Todo {
    std::string id;
    std::string title;
    bool completed = false;
};

struct BoolField {
    bool present; // is the key there at all?
    bool valid;   // if present, is it actually true/false
    bool value;   // the parsed value (only meaningful when present and valid)
};