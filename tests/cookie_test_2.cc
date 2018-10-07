#include "gtest/gtest.h"

#include <pistache/cookie.h>
#include <pistache/date.h>

using namespace Pistache;
using namespace Pistache::Http;

void addCookies(const char* str, std::function<void (const CookieJar&)> testFunc) {
    CookieJar jar;
    jar.addFromRaw(str, strlen(str));
    testFunc(jar);
}

TEST(cookie_test_2, cookiejar_test_2) {

    addCookies("key=value1; key=value2; key2=; key2=foo=bar", [](const CookieJar& jar) {
        int count = 0;

        for (const auto& c: jar) {
			count++;
		}

        ASSERT_EQ(count,4); // number of cookies must be 4 in this case

    });
    
}
