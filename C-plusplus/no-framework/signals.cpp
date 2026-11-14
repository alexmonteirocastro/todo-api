#include "signals.h"

volatile sig_atomic_t running = 1; // the shutdown flag

void handle_sigint(int) {
    running = 0; // Ctrl-C flips it to zero
}

void install_signal_handlers() {
     struct sigaction sa{};
    sa.sa_handler = handle_sigint;
    /**
     * By default, many systems auto-resume a syscall that a signal interrupts (that’s SA_RESTART), 
     * which would mean accept just goes back to sleep and never notices running changed — our server would ignore Ctrl-C. 
     * By not setting that flag, the signal makes accept() return -1 with errno == EINTR, 
     * which breaks us out so the while (running) check can see the flag and exit.
     */
    sigaction(SIGINT, &sa, nullptr); // NOTE: no SA_RESTART on purpose
}