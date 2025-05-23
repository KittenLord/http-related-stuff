#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include <stream.h>
#include <alloc.h>

#include <http/uri.c>

void skipTest(Stream *s) {
    MaybeChar c;
    while(isJust(c = stream_peekChar(s)) && c.value != '$') {
        stream_routeLine(s, null, true);
    }
    c = stream_routeLine(s, null, true);
}

int main() {
    String testsPath = mkString("./testing-data.txt");
    FILE *testingData = fopen((char *)testsPath.s, "r");
    if(!testingData) { printf("bad\n"); return 1; }
    int fd = fileno(testingData);
    Stream tests = mkStreamFd(fd);
    stream_rbufferEnable(&tests, 2048);

    MaybeChar c;

    int totalInvalidTests = 0;
    int detectedInvalidTests = 0;

    int totalValidTests = 0;
    int detectedValidTests = 0;
    int correctValidTests = 0;

    Alloc *resultAlloc = ALLOC_PUSH(mkAlloc_LinearExpandableC(16000));
    while(isJust(c = stream_peekChar(&tests))) {
        ResetC(resultAlloc);
        while(c.value == '\n') { c = stream_routeLine(&tests, null, true); }
        if(isNone(stream_peekChar(&tests))) break;
        Stream s;

        // Consume title
        StringBuilder sbTitle = mkStringBuilder();
        s = mkStreamSb(&sbTitle);
        stream_routeLine(&tests, &s, false);

        // Consume URI
        StringBuilder sbUri = mkStringBuilder();
        s = mkStreamSb(&sbUri);
        stream_routeLine(&tests, &s, false);
        Stream uriStream =mkStreamStr(sb_build(sbUri));
        Uri uri;
        UseAlloc(mkAlloc_LinearExpandableA(resultAlloc), {
            uri = Uri_parseUri(&uriStream, resultAlloc);
        });

        // Test validity
        StringBuilder sbTemp = mkStringBuilder();
        s = mkStreamSb(&sbTemp);
        stream_routeLine(&tests, &s, false);
        bool isValid = str_equal(sb_build(sbTemp), mkString("VALID"));
        if(isValid) totalValidTests++;
        if(!isValid) {
            totalInvalidTests++;
            stream_routeLine(&tests, null, true); // consume the $ line
            if(uri.error) { 
                detectedInvalidTests++;
            }
            else {
                printf("FAILED TEST: [%s]\n", sbTitle.s.s);
                printf("    Test Uri: \"%s\"\n", sbUri.s.s);
                printf("    - False positive\n\n");
                skipTest(&tests);
            }
            continue;
        }

        if(uri.error) {
            printf("FAILED TEST: [%s]\n", sbTitle.s.s);
            printf("    Test Uri: \"%s\"\n", sbUri.s.s);
            printf("    - False negative: %s\n\n", uri.errmsg.s);
            skipTest(&tests);
            continue;
        }

        detectedValidTests++;

        StringBuilder sbComp = mkStringBuilder();

        // Check scheme
        sb_reset(&sbTemp);
        stream_routeLine(&tests, &s, false);
        sb_appendString(&sbComp, mkString("Scheme: "));
        sb_appendString(&sbComp, uri.scheme);
        if(!str_equal(sb_build(sbTemp), sb_build(sbComp))) {
            printf("FAILED TEST: [%s]\n", sbTitle.s.s);
            printf("    Test Uri: \"%s\"\n", sbUri.s.s);
            printf("    - Schemes not equal:\n");
            printf("        Test:   %s\n", sbTemp.s.s);
            printf("        Parsed: %s\n\n", sbComp.s.s);
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
                    stream_routeLine(&tests, &s, false);
                    sb_reset(&sbComp);
                    sb_appendString(&sbComp, mkString("Userinfo: "));
                    sb_appendString(&sbComp, authority.userInfo);
                    if(!str_equal(sb_build(sbTemp), sb_build(sbComp))) {
                        printf("FAILED TEST: [%s]\n", sbTitle.s.s);
                        printf("    Test Uri: \"%s\"\n", sbUri.s.s);
                        printf("    - Userinfos not equal:\n");
                        printf("        Test:   %s\n", sbTemp.s.s);
                        printf("        Parsed: %s\n\n", sbComp.s.s);
                        skipTest(&tests);
                        continue;
                    }
                }


                sb_reset(&sbTemp);
                stream_routeLine(&tests, &s, false);
                sb_reset(&sbComp);
                sb_appendString(&sbComp, mkString("Host: "));

                if(authority.host.type == URI_HOST_REGNAME) {
                    sb_appendString(&sbComp, authority.host.regName);
                }
                else if(authority.host.type == URI_HOST_IPV4) {
                    String ipv4 = Uri_ipv4ToString(authority.host.ipv4, ALLOC);
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
                    printf("FAILED TEST: [%s]\n", sbTitle.s.s);
                    printf("    Test Uri: \"%s\"\n", sbUri.s.s);
                    printf("    - Hosts not equal:\n");
                    printf("        Test:   %s\n", sbTemp.s.s);
                    printf("        Parsed: %s\n\n", sbComp.s.s);
                    skipTest(&tests);
                    continue;
                }


                if(authority.hasPort) {
                    sb_reset(&sbTemp);
                    stream_routeLine(&tests, &s, false);
                    sb_reset(&sbComp);
                    sb_appendString(&sbComp, mkString("Port: "));
                    sb_appendString(&sbComp, authority.portString);
                    if(!str_equal(sb_build(sbComp), sb_build(sbTemp))) {
                        printf("FAILED TEST: [%s]\n", sbTitle.s.s);
                        printf("    Test Uri: \"%s\"\n", sbUri.s.s);
                        printf("    - Ports not equal:\n");
                        printf("        Test:   %s\n", sbTemp.s.s);
                        printf("        Parsed: %s\n\n", sbComp.s.s);
                        skipTest(&tests);
                        continue;
                    }
                }
            }

            UriPath path = uri.hierarchyPart.path;

            bool pathError = false;
            dynar_foreach(String, &path.segments) {
                sb_reset(&sbTemp);
                stream_routeLine(&tests, &s, false);
                sb_reset(&sbComp);
                sb_appendString(&sbComp, mkString("Path: "));
                sb_appendString(&sbComp, loop.it);
                if(!str_equal(sb_build(sbTemp), sb_build(sbComp))) {
                    printf("FAILED TEST: [%s]\n", sbTitle.s.s);
                    printf("    Test Uri: \"%s\"\n", sbUri.s.s);
                    printf("    - Paths not equal:\n");
                    printf("        Test:   %s\n", sbTemp.s.s);
                    printf("        Parsed: %s\n\n", sbComp.s.s);
                    skipTest(&tests);
                    pathError = true;
                    break;
                }
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
            stream_routeLine(&tests, &s, false);
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
            stream_routeLine(&tests, &s, false);
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
