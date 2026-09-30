// SPDX-License-Identifier: MIT
// Emulator-only FreeBSD/PS4 signal ABI probe. Included by the generated host,
// never by the production PS4 player.
struct mono_guest_sigset { unsigned bits[4]; };
struct mono_guest_sigaction {
    void (*handler)(int, void *, void *);
    int flags;
    struct mono_guest_sigset mask;
};
static volatile int mono_signal_count, mono_signal_arguments;
static void mono_test_signal(int number, void *info, void *context) {
    mono_signal_arguments = number == 66 && info && *(int *)info == 66 && context;
    ++mono_signal_count;
}
static int check_mono_signals(void) {
    int (*action)(int, const struct mono_guest_sigaction *, struct mono_guest_sigaction *);
    void *(*self)(void);
    int (*send)(void *, int);
    int (*mask)(int, const struct mono_guest_sigset *, struct mono_guest_sigset *);
    if (!resolve(kernel_handle, "sigaction", (void **)&action) ||
        !resolve(kernel_handle, "pthread_self", (void **)&self) ||
        !resolve(kernel_handle, "pthread_kill", (void **)&send) ||
        !resolve(kernel_handle, "pthread_sigmask", (void **)&mask)) return 0;
    struct mono_guest_sigaction original = {0}, requested = {0}, observed = {0};
    struct mono_guest_sigset block = {{0}}, previous = {{0}};
    requested.handler = mono_test_signal;
    requested.flags = 0x40; // FreeBSD SA_SIGINFO (different from Linux).
    block.bits[(66 - 1) / 32] = 1u << ((66 - 1) % 32);
    if (action(66, &requested, &original)) return 0;
    int okay = 0, masked = 0;
    if (action(66, NULL, &observed) || observed.handler != mono_test_signal || observed.flags != 0x40)
        goto done;
    if (mask(1, &block, &previous)) goto done; // SIG_BLOCK
    masked = 1;
    if (send(self(), 66) || mono_signal_count != 0) goto done;
    if (mask(3, &previous, NULL)) goto done; // SIG_SETMASK
    masked = 0;
    if (mono_signal_count != 1 || !mono_signal_arguments) goto done;
    // A second query must not erase the installed handler.
    if (action(66, NULL, &observed) || send(self(), 66) || mono_signal_count != 2) goto done;
    okay = 1;
done:
    if (masked) mask(3, &previous, NULL);
    if (action(66, &original, NULL)) okay = 0;
    report(okay ? "PASS signals: query, mask, delivery, guest arguments, restore" : "FAIL signals");
    return okay;
}
