#include "uuid.h"
#include <random>
#include <array>
#include <cctype> // std::isxdigit
#include <cstdio>

std::string generate_uuid() {
    static std::random_device rd;                     // 1. the seed source
    static std::mt19937 gen(rd());                    // 2. the generator engine
    std::uniform_int_distribution<int> dist(0, 255);  // 3. the shaper

    std::array<unsigned char, 16> b{};
    // auto = “let the compiler figure out the type” (it’s unsigned char here).
    // fill this slot with a fresh random 0–255 value. dist(gen) produces an int;
    for (auto& byte : b) byte = static_cast<unsigned char>(dist(gen));
    b[6] = (b[6] & 0x0F) | 0x40; // version 4
    b[8] = (b[8] & 0x3F) | 0x80; // version 10xx
    
    char out[37]; // a plain buffer. 32 hex digits + 4 hyphens + 1 null terminator = 37.
    std::snprintf(out, sizeof(out), 
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]
    );

    return std::string(out); // wrap the C char[] back into a std::string to return. (This is the reverse of .c_str())
}

bool is_valid_uuid (const std::string& s) {
    if (s.size() != 36) return false; // a UUID is always 36 chars long!

    for (size_t i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false; // them four spots must be hyphens
        } else {
            if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false; // all other charcaters must be hex
        }
    }

    return true;
}
