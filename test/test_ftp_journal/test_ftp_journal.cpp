#include <unity.h>
#include "../../src/ftp_journal_core.cpp"   // pure logic; native env does not build src/

// Unity exige estes hooks (chamados antes/depois de cada teste).
void setUp(void) {}
void tearDown(void) {}

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

void test_needs_upload(void) {
    ftpj::JournalMap j;
    j["a.txt"] = 100;
    TEST_ASSERT_TRUE(ftpj::needs_upload(j, "b.txt", 50));    // ausente
    TEST_ASSERT_TRUE(ftpj::needs_upload(j, "a.txt", 101));   // tamanho mudou
    TEST_ASSERT_FALSE(ftpj::needs_upload(j, "a.txt", 100));  // igual
}

void test_remote_path(void) {
    TEST_ASSERT_EQUAL_STRING("/up/2026/06/a.txt",
        ftpj::remote_path("/up", "2026/06/a.txt").c_str());
    TEST_ASSERT_EQUAL_STRING("/2026/06/a.txt",
        ftpj::remote_path("/", "2026/06/a.txt").c_str());
    TEST_ASSERT_EQUAL_STRING("/up/a.txt",
        ftpj::remote_path("/up/", "a.txt").c_str());
}

void test_remote_dirs(void) {
    auto d = ftpj::remote_dir_components("/up", "2026/06/a.txt");
    TEST_ASSERT_EQUAL_INT(3, (int)d.size());
    TEST_ASSERT_EQUAL_STRING("/up", d[0].c_str());
    TEST_ASSERT_EQUAL_STRING("/up/2026", d[1].c_str());
    TEST_ASSERT_EQUAL_STRING("/up/2026/06", d[2].c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_basic);
    RUN_TEST(test_parse_ignores_blank_and_malformed);
    RUN_TEST(test_serialize_roundtrip);
    RUN_TEST(test_needs_upload);
    RUN_TEST(test_remote_path);
    RUN_TEST(test_remote_dirs);
    return UNITY_END();
}
