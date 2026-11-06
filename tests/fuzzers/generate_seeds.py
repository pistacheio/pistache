#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Pistache project contributors
#
# SPDX-License-Identifier: Apache-2.0
"""
Deterministic structured seed generator for the fuzz_parsers harness.

The upstream corpus (tests/fuzzers/corpus) holds only 19 hand-picked seeds,
several of them single header values, which leaves the deep HTTP parsing
paths in src/common/http.cc (chunked-fragment edges, multi-header messages,
query forms, response parsing, route-tree shapes) reachable but unreached.
This script constructs structurally-valid and boundary-condition inputs for
every dispatch type of fuzz_parser.cpp:

  'R' request parsing    'A' response parsing   'H' header values
  'C' cookie parsing     'S' route segment tree  'O' URI query strings

Run from the OSS-Fuzz build script; the output directory is zipped into the
seed corpus next to the static corpus. Deterministic: no randomness, no
external samples, so reruns produce byte-identical seeds.
"""

import os
import sys

CRLF = "\r\n"


def seed(dispatch: str, payload: str) -> bytes:
    return dispatch.encode("ascii") + payload.encode("utf-8")


def gen_requests():
    """'R': structurally-valid requests and parse edges."""
    out = []

    # Simple GET with typical headers.
    out.append(seed("R", "GET /index.html HTTP/1.1" + CRLF
                    + "Host: example.com" + CRLF
                    + "User-Agent: Mozilla/5.0" + CRLF
                    + "Accept: */*" + CRLF + CRLF))

    # GET with query string and several Accept entries with q-values.
    out.append(seed("R", "GET /search?q=pistache&lang=en&safe=off HTTP/1.1" + CRLF
                    + "Host: www.example.com:8080" + CRLF
                    + "Accept: text/html;q=1.0,application/json;q=0.8,*;q=0.1" + CRLF
                    + "Accept-Encoding: gzip, deflate, br;q=0" + CRLF
                    + "Connection: keep-alive" + CRLF + CRLF))

    # POST with Content-Length body.
    out.append(seed("R", "POST /submit HTTP/1.1" + CRLF
                    + "Host: api.example.com" + CRLF
                    + "Content-Type: application/x-www-form-urlencoded" + CRLF
                    + "Content-Length: 18" + CRLF + CRLF
                    + "job=100&priority=5"))

    # POST with a chunked body, multi-chunk, lowercase and uppercase sizes,
    # chunk extensions and the terminating zero chunk.
    out.append(seed("R", "POST /upload HTTP/1.1" + CRLF
                    + "Host: up.example.com" + CRLF
                    + "Transfer-Encoding: chunked" + CRLF + CRLF
                    + "7" + CRLF + "Mozilla" + CRLF
                    + "11" + CRLF + "Developer Network" + CRLF
                    + "0" + CRLF + CRLF))
    out.append(seed("R", "POST /upload2 HTTP/1.1" + CRLF
                    + "Host: up.example.com" + CRLF
                    + "Transfer-Encoding: chunked" + CRLF + CRLF
                    + "A;ext=1;q=2" + CRLF + "0123456789" + CRLF
                    + "1F" + CRLF + "chunk-of-thirtyone-bytes!!!" + CRLF
                    + "0" + CRLF + "X-Trailer: v" + CRLF + CRLF))

    # HTTP/1.0 request without Content-Length.
    out.append(seed("R", "GET /old HTTP/1.0" + CRLF
                    + "Host: old.example.com" + CRLF + CRLF))

    # Headers covering every registered name (parse side of the message path).
    headers = [
        "Access-Control-Allow-Origin: *",
        "Access-Control-Allow-Headers: X-A, X-B",
        "Access-Control-Expose-Headers: X-C",
        "Access-Control-Allow-Methods: GET, POST, DELETE",
        "Allow: GET, HEAD, OPTIONS",
        "Cache-Control: max-age=3600, no-cache, max-stale=0",
        "Connection: close",
        "Content-Encoding: gzip",
        "Transfer-Encoding: chunked",
        "Content-Length: 42",
        "Content-Type: application/vnd.api+json;charset=utf-8",
        "Authorization: Bearer tok.en",
        "ETag: W/\"weak-tag\"",
        "Date: Sun, 06 Nov 1994 08:49:37 GMT",
        "Expect: 100-continue",
        "Host: [::1]:9090",
        "Last-Modified: Fri, 11 Feb 2022 16:00:00 GMT",
        "Location: /moved/here",
        "Server: pistache/1.0 (fuzz-seed)",
        "User-Agent: seed-agent/0.1",
    ]
    out.append(seed("R", "GET /allheaders HTTP/1.1" + CRLF
                    + CRLF.join(headers) + CRLF + CRLF))

    # Cookie header with expiry date formats.
    out.append(seed("R", "GET /cookie HTTP/1.1" + CRLF
                    + "Host: c.example.com" + CRLF
                    + "Cookie: session=abc123; Path=/; HttpOnly; "
                      "Expires=Sat, 11 Feb 2028 16:30:00 GMT" + CRLF + CRLF))

    # Boundary conditions.
    out.append(seed("R", "GET / HTTP/1.1" + CRLF + "Host:" + CRLF + CRLF))
    out.append(seed("R", "GET http://proxy.example.com:3128/target HTTP/1.1"
                    + CRLF + "Host: proxy.example.com" + CRLF + CRLF))
    out.append(seed("R", "OPTIONS * HTTP/1.1" + CRLF
                    + "Host: x.example.com" + CRLF + CRLF))
    return out


def gen_responses():
    """'A': structurally-valid responses (dark in the current corpus)."""
    out = []

    out.append(seed("A", "HTTP/1.1 200 OK" + CRLF
                    + "Content-Type: text/html" + CRLF
                    + "Content-Length: 5" + CRLF + CRLF
                    + "hello"))
    out.append(seed("A", "HTTP/1.1 404 Not Found" + CRLF
                    + "Server: nginx" + CRLF
                    + "Content-Length: 0" + CRLF + CRLF))
    out.append(seed("A", "HTTP/1.1 301 Moved Permanently" + CRLF
                    + "Location: https://example.com/new" + CRLF
                    + "Content-Length: 0" + CRLF + CRLF))
    out.append(seed("A", "HTTP/1.1 401 Unauthorized" + CRLF
                    + "WWW-Authenticate: Basic realm=\"sec\"" + CRLF
                    + "Content-Length: 0" + CRLF + CRLF))
    out.append(seed("A", "HTTP/1.1 200 OK" + CRLF
                    + "Transfer-Encoding: chunked" + CRLF + CRLF
                    + "4" + CRLF + "Wiki" + CRLF
                    + "5" + CRLF + "pedia" + CRLF
                    + "E" + CRLF + " in\r\n\r\nchunks." + CRLF
                    + "0" + CRLF + CRLF))
    out.append(seed("A", "HTTP/1.0 500 Internal Server Error" + CRLF
                    + "Content-Length: 3" + CRLF + CRLF
                    + "err"))
    out.append(seed("A", "HTTP/1.1 204 No Content" + CRLF
                    + "Cache-Control: no-store" + CRLF + CRLF))
    out.append(seed("A", "HTTP/1.1 100 Continue" + CRLF + CRLF))
    return out


def gen_header_values():
    """'H': header values for the types the corpus already dispatches to."""
    out = []
    values = [
        "Sun, 06 Nov 1994 08:49:37 GMT",
        "Sunday, 06-Nov-94 08:49:37 GMT",
        "Sun Nov  6 08:49:37 1994",
        "Thu, 01 Jan 1970 00:00:00 GMT",
        "no-cache, max-age=0, must-revalidate, proxy-revalidate",
        "max-stale=100, min-fresh=30, only-if-cached",
        "close",
        "keep-alive",
        "Upgrade, HTTP/2.0, SHTTP/1.3",
        "gzip;q=1.0, identity;q=0.5, *;q=0",
        "compress, gzip",
        "chunked",
        "29",
        "0",
        "text/html;charset=iso-8859-4",
        "application/json;level=1;q=0.9",
        "Basic dXNlcjpwYXNzd29yZA==",
        "Bearer abc.def.ghi",
        "Basic",
        "100-continue",
        "example.com:8080",
        "[2001:db8::1]",
        "pistache/0.1 (seed)",
        "Apache/2.4.1 (Unix) OpenSSL/1.0.0",
    ]
    for value in values:
        out.append(seed("H", value))
    return out


def gen_cookies():
    """'C': cookie strings with attribute and date variety."""
    out = []
    cookies = [
        "user_id=123456; Max-Age=3600; Path=/account; Domain=example.com; "
        "Secure; HttpOnly; Expires=Sat, 11 Feb 2028 16:30:00 GMT",
        "a=1",
        "a=",
        "=value",
        "a=1; b=2; c=3",
        "a=1; Expires=Wednesday, 09-Nov-99 23:12:40 GMT",
        "a=1; Expires=Wed Nov  9 23:12:40 2039",
        "a=1; SameSite=Strict",
        "a=1; SameSite=Lax; Priority=High",
        "name-with-dashes=value-with-dashes",
    ]
    for value in cookies:
        out.append(seed("C", value))
    return out


def gen_routes():
    """'S': segment-tree lines, one route per line, suffix op A/R/F."""
    out = []
    route_sets = [
        ["users/:userid/A", "users/me/F"],
        ["static/A", "static/F", "stat/R"],
        ["a/b/c/d/A", "a/*/d/A", "a/b/F"],
        ["x/:p?/A", "x/F", "x/y/F"],
        ["*/api/A", "v2/api/A", "v2/api/F"],
        ["deep/:a/:b/:c/A", "deep/1/2/3/F", "deep/1/R"],
    ]
    for routes in route_sets:
        out.append(seed("S", "\n".join(routes) + "\n"))
    return out


def gen_queries():
    """'O': URI query strings."""
    out = []
    queries = [
        "q=pistache",
        "a=1&b=2&c=3",
        "key=",
        "=value",
        "percent%20encoded=%2Fslash%2F",
        "a=1&a=2",
        "+plus+=sign",
    ]
    for value in queries:
        out.append(seed("O", value))
    return out


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "corpus-generated"
    os.makedirs(out_dir, exist_ok=True)
    seeds = (gen_requests() + gen_responses() + gen_header_values()
             + gen_cookies() + gen_routes() + gen_queries())
    for index, blob in enumerate(seeds):
        name = os.path.join(out_dir, "gen_%03d.seed" % index)
        with open(name, "wb") as handle:
            handle.write(blob)
    print("generated %d seeds in %s" % (len(seeds), out_dir))


if __name__ == "__main__":
    main()
