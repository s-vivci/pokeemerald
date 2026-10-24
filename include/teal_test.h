#ifndef GUARD_TEAL_TEST_H
#define GUARD_TEAL_TEST_H

// Test-only boot path for Pokemon Teal (off by default).
// Build with `make TEAL_TEST=1` to get a ROM that skips the intro and loops
// through a fixed list of single trainer battles, for headless end-to-end tests.
void TealTest_Start(void);

#endif // GUARD_TEAL_TEST_H
