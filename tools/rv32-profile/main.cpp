// Runs a cart under QEMU for the RV32 profiler (see profile.sh).  Frames
// between the two sim_marker() calls are the ones measured.
#include "p8p/cart.h"
#include "p8p/runtime.h"
#include <stdio.h>
#include <string.h>
#include "cart_data.h"

extern "C" __attribute__((noinline)) void sim_marker(int phase) {
    static volatile int marker_phase;
    marker_phase = phase;
    asm volatile("" ::: "memory");
}

int main() {
    p8p_cart_t cart = {};
    int failed = memcmp(cart_data, "\x89PNG", 4) == 0 ?
        p8p_cart_load_png_memory(cart_data, cart_data_len, &cart) :
        p8p_cart_load_text_memory(cart_data, cart_data_len, &cart);
    if (failed) { puts("cart load failed"); return 2; }
    p8p_runtime_t *rt = p8p_runtime_create();
    if (p8p_runtime_load(rt, &cart)) { puts(p8p_runtime_error(rt)); return 3; }
    int value = 0;
    if (SETUP[0] && p8p_runtime_debug_eval_int(rt, SETUP, &value))
        puts("setup expression failed");
    for (int f = 0; f < WARM; ++f) p8p_runtime_step(rt, 0);
    sim_marker(1);
    for (int f = 0; f < FRAMES; ++f) p8p_runtime_step(rt, 0);
    sim_marker(2);
    puts("done");
    return 0;
}
