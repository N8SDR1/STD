/* Console check for the RADE shim. Not linked into std.exe. */
#include <stdio.h>

int std_rade_selftest(void);
const char* std_rade_error(void);

int main(void) {
  int rc = std_rade_selftest();
  printf("selftest %d %s\n", rc, std_rade_error());
  return rc == 0 ? 0 : 1;
}
