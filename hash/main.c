#include <stdlib.h>
#include <stdio.h>

#include "sha.c"

int main() {
    Mem raw = mkString("");
    Sha_Result160 hash = Sha_Sha1(raw);
    write(STDOUT_FILENO, hash.data, 160 / 8);

    // printf("Hello, World!\n");
    return 0;
}
