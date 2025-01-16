#include <stdio.h>
#include <stdlib.h>

#include <stream.h>
#include <alloc.h>
#include "uri.c"

#define URIS_COUNT 8
int main() {
    // TODO: add a shitton of test cases (these are taken from RFC3986 itself)
    String uris[URIS_COUNT] = {
        mkString("ftp://ftp.is.co.za/rfc/rfc1808.txt"),
        mkString("http://www.ietf.org/rfc/rfc2396.txt"),
        mkString("ldap://[2001:db8::7]/c=GB?objectClass?one"),
        mkString("mailto:John.Doe@example.com"),
        mkString("news:comp.infosystems.www.servers.unix"),
        mkString("tel:+1-816-555-1212"),
        mkString("telnet://192.0.2.16:80/"),
        mkString("urn:oasis:names:specification:docbook:dtd:xml:4.1.2"),
    };

    for(int i = 0; i < URIS_COUNT; i++) {
        PeekStream s = mkPeekStream(mkStreamStr(uris[i]));

        Uri uri;
        Alloc resultAlloc = mkAlloc_LinearExpandable();
        UseAlloc(mkAlloc_LinearExpandable(), {
            uri = Uri_parseUri(&s, &resultAlloc);
        });

        if(uri.error) {
            printf("Test %d failed\n    Reason: %s\n    Stream is at position: %d\n", i, uri.errmsg.s, s.s.pos);
        }
    }

    return 0;
}
