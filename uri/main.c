#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include <stream.h>
#include <alloc.h>
#include "uri.c"

void skipTest(PeekStream *s) {
    MaybeChar c;
    while(isJust(c = pstream_peekChar(s)) && c.value != '$') {
        c = pstream_routeLine(s, null, true);
    }
    c = pstream_routeLine(s, null, true);
}

#define URIS_COUNT 8
int main() {
    String testsPath = mkString("./testing-data.txt");
    FILE *testingData = fopen(testsPath.s, "r");
    if(!testingData) { printf("bad\n"); return 1; }
    int fd = fileno(testingData);
    PeekStream tests = mkPeekStream(mkStreamFd(fd));
    MaybeChar c;

    int totalInvalidTests = 0;
    int detectedInvalidTests = 0;

    int totalValidTests = 0;
    int detectedValidTests = 0;
    int correctValidTests = 0;

    Alloc resultAlloc = mkAlloc_LinearExpandable();
    ALLOC_PUSH(resultAlloc);
    while(isJust(c = pstream_peekChar(&tests))) {
        Reset();
        while(c.value == '\n') { c = pstream_routeLine(&tests, null, true); }
        if(isNone(pstream_peekChar(&tests))) break;
        Stream s;

        // Consume title
        StringBuilder sbTitle = mkStringBuilder();
        s = mkStreamSb(&sbTitle);
        pstream_routeLine(&tests, &s, false);

        // Consume URI
        StringBuilder sbUri = mkStringBuilder();
        s = mkStreamSb(&sbUri);
        pstream_routeLine(&tests, &s, false);
        PeekStream uriStream = mkPeekStream(mkStreamStr(sb_build(sbUri)));
        Uri uri;
        UseAlloc(mkAlloc_LinearExpandable(), {
            uri = Uri_parseUri(&uriStream, &resultAlloc);
        });

        // Test validity
        StringBuilder sbTemp = mkStringBuilder();
        s = mkStreamSb(&sbTemp);
        pstream_routeLine(&tests, &s, false);
        bool isValid = str_equal(sb_build(sbTemp), mkString("VALID"));
        if(isValid) totalValidTests++;
        if(!isValid) {
            totalInvalidTests++;
            pstream_routeLine(&tests, null, true); // consume the $ line
            if(uri.error) { 
                detectedInvalidTests++;
            }
            else {
                printf("FAILED TEST: [%s]\n", sbTitle.s);
                printf("    Test Uri: \"%s\"\n", sbUri.s);
                printf("    - False positive\n\n");
                skipTest(&tests);
            }
            continue;
        }

        if(uri.error) {
            printf("FAILED TEST: [%s]\n", sbTitle.s);
            printf("    Test Uri: \"%s\"\n", sbUri.s);
            printf("    - False negative: %s\n\n", uri.errmsg.s);
            skipTest(&tests);
            continue;
        }

        detectedValidTests++;

        StringBuilder sbComp = mkStringBuilder();

        // Check scheme
        sb_reset(&sbTemp);
        pstream_routeLine(&tests, &s, false);
        sb_appendString(&sbComp, mkString("Scheme: "));
        sb_appendString(&sbComp, uri.scheme);
        if(!str_equal(sb_build(sbTemp), sb_build(sbComp))) {
            printf("FAILED TEST: [%s]\n", sbTitle.s);
            printf("    Test Uri: \"%s\"\n", sbUri.s);
            printf("    - Schemes not equal:\n");
            printf("        Test:   %s\n", sbTemp.s);
            printf("        Parsed: %s\n\n", sbComp.s);
            skipTest(&tests);
            continue;
        }

        if(uri.hierarchyPart.type == URI_HIER_EMPTY) {
            // no fields to check
        }
        else if(uri.hierarchyPart.type == URI_HIER_ROOTLESS ||
                uri.hierarchyPart.type == URI_HIER_ABSOLUTE ||
                uri.hierarchyPart.type == URI_HIER_AUTHORITY) {
            if(uri.hierarchyPart.type == URI_HIER_AUTHORITY) {
                UriAuthority authority = uri.hierarchyPart.authority;

                if(authority.hasUserInfo) {
                    sb_reset(&sbTemp);
                    pstream_routeLine(&tests, &s, false);
                    sb_reset(&sbComp);
                    sb_appendString(&sbComp, mkString("Userinfo: "));
                    sb_appendString(&sbComp, authority.userInfo);
                    if(!str_equal(sb_build(sbTemp), sb_build(sbComp))) {
                        printf("FAILED TEST: [%s]\n", sbTitle.s);
                        printf("    Test Uri: \"%s\"\n", sbUri.s);
                        printf("    - Userinfos not equal:\n");
                        printf("        Test:   %s\n", sbTemp.s);
                        printf("        Parsed: %s\n\n", sbComp.s);
                        skipTest(&tests);
                        continue;
                    }
                }


                sb_reset(&sbTemp);
                pstream_routeLine(&tests, &s, false);
                sb_reset(&sbComp);
                sb_appendString(&sbComp, mkString("Host: "));

                if(authority.host.type == URI_HOST_REGNAME) {
                    sb_appendString(&sbComp, authority.host.regName);
                }
                else if(authority.host.type == URI_HOST_IPV4) {
                    String ipv4 = Uri_ipv4ToString(authority.host.ipv4, &ALLOC);
                    sb_appendString(&sbComp, ipv4);
                }
                else if(authority.host.type == URI_HOST_IPLITERAL) {
                    printf("Invalid host type\n");
                    skipTest(&tests);
                    continue;
                }
                else {
                    printf("Invalid host type\n");
                    skipTest(&tests);
                    continue;
                }

                if(!str_equal(sb_build(sbTemp), sb_build(sbComp))) {
                    printf("FAILED TEST: [%s]\n", sbTitle.s);
                    printf("    Test Uri: \"%s\"\n", sbUri.s);
                    printf("    - Hosts not equal:\n");
                    printf("        Test:   %s\n", sbTemp.s);
                    printf("        Parsed: %s\n\n", sbComp.s);
                    skipTest(&tests);
                    continue;
                }


                if(authority.hasPort) {
                    sb_reset(&sbTemp);
                    pstream_routeLine(&tests, &s, false);
                    sb_reset(&sbComp);
                    sb_appendString(&sbComp, mkString("Port: "));
                    sb_appendString(&sbComp, authority.portString);
                    if(!str_equal(sb_build(sbComp), sb_build(sbTemp))) {
                        printf("FAILED TEST: [%s]\n", sbTitle.s);
                        printf("    Test Uri: \"%s\"\n", sbUri.s);
                        printf("    - Ports not equal:\n");
                        printf("        Test:   %s\n", sbTemp.s);
                        printf("        Parsed: %s\n\n", sbComp.s);
                        skipTest(&tests);
                        continue;
                    }
                }
            }

            UriPath path = uri.hierarchyPart.path;
            UriPathSegment *current = path.segments;

            bool pathError = false;
            while(current) {
                sb_reset(&sbTemp);
                pstream_routeLine(&tests, &s, false);
                sb_reset(&sbComp);
                sb_appendString(&sbComp, mkString("Path: "));
                sb_appendString(&sbComp, current->segment);
                if(!str_equal(sb_build(sbTemp), sb_build(sbComp))) {
                    printf("FAILED TEST: [%s]\n", sbTitle.s);
                    printf("    Test Uri: \"%s\"\n", sbUri.s);
                    printf("    - Paths not equal:\n");
                    printf("        Test:   %s\n", sbTemp.s);
                    printf("        Parsed: %s\n\n", sbComp.s);
                    skipTest(&tests);
                    pathError = true;
                    break;
                }

                current = current->next;
            }

            if(pathError) continue;
        }
        else {
            printf("Invalid hier type\n");
            skipTest(&tests);
            continue;
        }


        if(uri.hasQuery) {
            sb_reset(&sbTemp);
            pstream_routeLine(&tests, &s, false);
            sb_reset(&sbComp);
            sb_appendString(&sbComp, mkString("Query: "));
            sb_appendString(&sbComp, uri.query);
            if(!str_equal(sb_build(sbTemp), sb_build(sbComp))) {
                skipTest(&tests);
                continue;
            }
        }


        if(uri.hasFragment) {
            sb_reset(&sbTemp);
            pstream_routeLine(&tests, &s, false);
            sb_reset(&sbComp);
            sb_appendString(&sbComp, mkString("Fragment: "));
            sb_appendString(&sbComp, uri.fragment);
            if(!str_equal(sb_build(sbTemp), sb_build(sbComp))) {
                skipTest(&tests);
                continue;
            }
        }

        correctValidTests++;


        skipTest(&tests);
    }
    ALLOC_POP();

    printf("Stats: \n");
    printf("Valid tests: %d [%d] / %d\n", correctValidTests, detectedValidTests, totalValidTests);
    printf("Invalid tests: %d / %d\n", detectedInvalidTests, totalInvalidTests);

    return 0;
}
