#pragma once
// ============================================================================
// uuid.h — UUIDv4 generation and validation. No dependency on anything else.
// ============================================================================
#include <string>

/**
 * `std::random_device rd;` — a source of real entropy from the OS (hardware randomness, /dev/urandom, etc.). 
 * It’s high-quality but slow, so we don’t use it for every number — you use it once, to seed the fast generator.
 * std::mt19937 gen(rd());` — the actual engine: a Mersenne Twister (the 19937 is its period, 2¹⁹⁹³⁷−1 — absurdly long before it repeats). 
 * It’s a pseudo-random generator: fast, deterministic given a seed. gen(rd()) seeds it by calling rd() once. 
 * Think of rd as striking the match, gen as the fire that burns fast after.
 * `std::uniform_int_distribution<int> dist(0, 255);` — the shaper. 
 * The engine spits out raw bits across its full range; 
 * the distribution maps those onto the range we actually want — here, 0 to 255 (one byte), uniformly (every value equally likely, no modulo bias). 
 * We use it as dist(gen) — “give me a shaped number, pulling raw bits from gen.”
 * 
 * Why the static on the first two? 
 * static inside a function means “create this once, on the first call, and keep it alive for the whole program.”
 *  Without it, every call to generate_uuid() would build a fresh random_device and re-seed a fresh engine — slow, 
 * and worse, two calls in the same instant could seed identically and produce the same UUID. 
 * static means the engine is created and seeded once, then just keeps advancing. 
 * (The dist is cheap and stateless, so it doesn’t need static — though it wouldn’t hurt.)
 */
std::string generate_uuid();

bool is_valid_uuid (const std::string& s);