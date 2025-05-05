#include <stdlib.h>
#include <stdio.h>

#include "sha.c"

int main() {
    Mem raw = mkString("Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vivamus interdum, arcu a ullamcorper aliquam, velit libero mattis diam, vel sagittis felis leo ut massa. Nam porttitor massa ac leo commodo, non suscipit nibh interdum. Donec augue magna, scelerisque vitae luctus id, placerat at metus aliquam.");
    Sha_Result256 hash = Sha_Sha256(raw);
    write(STDOUT_FILENO, hash.data, 256 / 8);

    return 0;
}
