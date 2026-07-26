// Compiled in only under -DMETAL_SELFTEST, so the hook that runs on every Write carries
// no test code. No framework: assertions and a count, same as the Python it replaced.
#include <cstdlib>

namespace {

int checks = 0;

void ok(bool cond, const char* what) {
    ++checks;
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

std::string ev(const char* name, const char* path, const std::string& content) {
    return std::string("{\"hook_event_name\":\"") + name + "\",\"tool_input\":{\"file_path\":\"" +
           path + "\",\"content\":\"" + hj::esc(content) + "\"}}";
}

std::string pre(const char* path, const std::string& content) {
    return check(ev("PreToolUse", path, content)).value_or("");
}

bool has(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

std::string lines(int n) { return std::string(static_cast<std::size_t>(n), '\n'); }

int selftest() {
    const std::string big = lines(400), mid = lines(250), small = lines(10);

    // the hard limit, unchanged
    ok(has(pre("/p/a.rs", big), "\"deny\""), "oversized write is denied");
    ok(pre("/p/a.rs", small).empty(), "small file is silent");
    ok(pre("/p/a.md", big).empty(), "docs and data are not source");
    ok(pre("/p/target/a.rs", big).empty(), "build output is not ours");
    ok(pre("", big).empty(), "no path, nothing to say");

    // the warn band allows, but says so, and says something different from the deny
    const std::string warn = pre("/p/a.rs", mid);
    ok(has(warn, "\"allow\""), "a warn that blocks is just a lower limit");
    ok(has(warn, "50 from the limit"), "the warn counts the runway");
    ok(!has(warn, "Split it now"), "warn is not the deny text");
    ok(pre("/p/a.rs", lines(kWarn - 1)).empty(), "quiet below the warn");

    // the override: granted, bounded, and it must cost a reason
    const std::string mark = "// metal: allow 380 - one lexer state machine, the table does not split\n";
    ok(pre("/p/lex.rs", mark + lines(350)).empty(), "a justified exception is allowed");
    ok(has(pre("/p/lex.rs", mark + lines(400)), "\"deny\""), "past its own allowance is denied");
    ok(has(pre("/p/lex.rs", "// metal: allow 380 - meh\n" + lines(350)), "\"deny\""),
       "a throwaway reason is not a reason");
    ok(has(pre("/p/lex.rs", "// metal: allow 9000 - a reason of entirely adequate length\n" +
                                lines(350)), "\"deny\""),
       "an unbounded exception is a repeal");
    ok(pre("/p/lex.py", "# metal: allow 380 - one lexer state machine, does not split\n" +
                            lines(350)).empty(),
       "comment syntax follows the language");
    ok(has(pre("/p/lex.rs", "int allow = 380; // not a marker, just code that says allow\n" +
                                lines(350)), "\"deny\""),
       "the word allow alone is not a grant");

    // the JSON has to be parsed, not searched: content can contain the key names
    const std::string trap = "const char* k = \"file_path\";\nstruct { int x; } tool_input;\n";
    ok(pre("/p/a.rs", trap + lines(400)).find("a.rs") != std::string::npos,
       "keys inside content do not confuse the parser");
    ok(has(pre("/p/a.rs", "\"quotes\" and \\ backslashes\n" + lines(400)), "\"deny\""),
       "escaped content decodes");
    ok(check("not json at all").has_value() == false, "garbage in, silence out");
    ok(check("{}").has_value() == false, "empty event, silence out");

    std::printf("ok (%d checks)\n", checks);
    return 0;
}

}  // namespace
