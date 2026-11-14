#pragma once
#include <csignal>

/**
 * the handler can fire at any instant, interrupting main mid-line. 
 * `sig_atomic_t` is a type guaranteed to be read/written in one uninterruptible step (no half-updated value), 
 * and `volatile` forbids the compiler from caching running in a register — it must re-read the real variable each loop, 
 * since it can change “from outside.” 
 * It’s the only kind of variable you may safely touch from a signal handler.
 */
extern volatile sig_atomic_t running; // announced here, read by main's loop
void install_signal_handlers();