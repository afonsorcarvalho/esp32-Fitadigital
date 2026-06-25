#include <unity.h>
#include "../../src/ftp_journal_core.cpp"   // pure logic; native env does not build src/

void test_parse_basic(void) {
    auto m = ftpj::parse_journal("2026/06/20260625.txt|14320\n2026/06/cycles.ndjson|512\n");
    TEST_ASSERT_EQUAL_INT(2, (int)m.size());
    TEST_ASSERT_EQUAL_INT64(14320, m["2026/06/20260625.txt"]);
    TEST_ASSERT_EQUAL_INT64(512, m["2026/06/cycles.ndjson"]);
}

void test_parse_ignores_blank_and_malformed(void) {
    auto m = ftpj::parse_journal("\n  \nbadline_no_pipe\n2026/a.txt|10\nx|notanumber\n");
    TEST_ASSERT_EQUAL_INT(1, (int)m.size());
    TEST_ASSERT_EQUAL_INT64(10, m["2026/a.txt"]);
}

void test_serialize_roundtrip(void) {
    ftpj::JournalMap m;
    m["2026/06/a.txt"] = 100;
    m["2026/06/b.txt"] = 200;
    std::string s = ftpj::serialize_journal(m);
    auto m2 = ftpj::parse_journal(s);
    TEST_ASSERT_EQUAL_INT(2, (int)m2.size());
    TEST_ASSERT_EQUAL_INT64(100, m2["2026/06/a.txt"]);
    TEST_ASSERT_EQUAL_INT64(200, m2["2026/06/b.txt"]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_basic);
    RUN_TEST(test_parse_ignores_blank_and_malformed);
    RUN_TEST(test_serialize_roundtrip);
    return UNITY_END();
}
