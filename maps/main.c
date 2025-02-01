#include <stdio.h>
#include <stdlib.h>

#include <map.h>
#include <hashmap.h>
#include <str.h>

int main() {
    Hashmap hm = mkHashmap(ALLOC_GLOBAL);

    hm_set(&hm, mkString("a"), mkString("value a"));
    hm_set(&hm, mkString("b"), mkString("value b"));
    hm_set(&hm, mkString("c"), mkString("value c"));

    String a = hm_get(&hm, mkString("a"));
    String b = hm_get(&hm, mkString("b"));
    String c = hm_get(&hm, mkString("c"));

    printf("'A' value: %s\n", a.s);
    printf("'B' value: %s\n", b.s);
    printf("'C' value: %s\n", c.s);

    printf("Len: %ld\n", hm.len);
    printf("Max depth: %ld\n", hm.max);
    printf("Total: %ld\n", hm.total);

    hm_set(&hm, mkString("a"), mkString("new value a"));
    hm_set(&hm, mkString("b"), mkString("new value b"));
    hm_set(&hm, mkString("c"), mkString("new value c"));
    hm_set(&hm, mkString("d"), mkString("value d"));

    a = hm_get(&hm, mkString("a"));
    b = hm_get(&hm, mkString("b"));
    c = hm_get(&hm, mkString("c"));
    String d = hm_get(&hm, mkString("d"));

    printf("'A' value: %s\n", a.s);
    printf("'B' value: %s\n", b.s);
    printf("'C' value: %s\n", c.s);
    printf("'D' value: %s\n", d.s);

    printf("Len: %ld\n", hm.len);
    printf("Max depth: %ld\n", hm.max);
    printf("Total: %ld\n", hm.total);

    String n = hm_get(&hm, mkString("n")); // hasn't been inserted
    printf("'N' pointer: %p\n", n.s);

    return 0;
}
