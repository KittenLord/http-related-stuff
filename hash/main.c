#include <stdlib.h>
#include <stdio.h>

#include "sha.c"

int main() {
    Mem raw = mkString("Lorem ipsum dolor sit amet, consectetur adipiscing elit. Phasellus ac molestie erat. Aliquam ultricies dictum diam, at ornare mi molestie ut. Integer mattis nulla dapibus, porta nibh id, consequat lectus. Proin elementum luctus ullamcorper. Suspendisse ex.");
    Sha_Result160 hash = Sha_Sha1(raw);
    write(STDOUT_FILENO, hash.data, 160 / 8);

    return 0;
}
