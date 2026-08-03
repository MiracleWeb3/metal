// Compiled in only under -DMETAL_SELFTEST, so the hook that runs on every Write carries
// no test code. No framework: assertions and a count, same as the Python it replaced.
#include <cstdlib>
#include <filesystem>

namespace {

namespace tfs = std::filesystem;

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

// --- rule 3 fixtures ---------------------------------------------------------------
// Under the working directory, not /tmp: is_throwaway() skips /tmp by design, so a
// fixture there would test nothing.

void put(const tfs::path& p, std::string_view body) {
    std::error_code ec;
    tfs::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary) << body;
}

std::string floor_at(const tfs::path& p) {
    return floor_check(ev("PostToolUse", p.string().c_str(), "")).value_or("");
}

int rule3(const tfs::path& root) {
    std::error_code ec;
    tfs::remove_all(root, ec);
    put(root / "good" / "build.sh",
        "flags=(-std=c++20 -O2 -Wall -Wextra -Werror)\n[ \"$m\" = asan ] && "
        "flags+=(-fsanitize=address,undefined)\n");
    put(root / "nowerror" / "CMakeLists.txt", "add_compile_options(-Wall -Wextra -Wpedantic)\n");
    put(root / "nosan" / "build.sh", "g++ -std=c++20 -O2 -Wall -Wextra -Werror main.cpp\n");
    put(root / "bare" / ".git", "");  // a repo root with nothing configured at all
    put(root / "ts-ok" / "tsconfig.json", "{\n  \"compilerOptions\": { \"strict\" : true }\n}\n");
    put(root / "ts-off" / "tsconfig.json", "{\n  \"compilerOptions\": {\"strict\": false}\n}\n");
    put(root / "py-ok" / "pyproject.toml", "[tool.mypy]\nstrict = true\n");
    put(root / "py-none" / "pyproject.toml", "[project]\nname = \"vibes\"\n");

    // C and C++ — the deepest instance
    ok(floor_at(root / "good" / "a.cpp").empty(), "a project that meets the floor is silent");
    const std::string drift = floor_at(root / "nowerror" / "a.cpp");
    ok(has(drift, "-Werror"), "the missing flag is named");
    ok(!has(drift, "missing -Wall"), "flags that are present are not reported missing");
    ok(has(drift, "CMakeLists.txt"), "the governing build file is named");
    ok(has(drift, "fsanitize"), "a sanitizer note rides along with a real violation");
    ok(floor_at(root / "nosan" / "a.cpp").empty(), "a met floor with no sanitizer stays silent");

    // TypeScript — strict: true is the same shape as -Werror
    ok(floor_at(root / "ts-ok" / "a.ts").empty(), "strict true is the floor, spacing tolerated");
    ok(has(floor_at(root / "ts-off" / "a.tsx"), "strict"), "strict false is reported");

    // Python — metal does not pick your type checker, only insists on one
    ok(floor_at(root / "py-ok" / "a.py").empty(), "a configured type checker satisfies it");
    ok(has(floor_at(root / "py-none" / "a.py"), "type checker"), "no checker at all is reported");

    ok(has(floor_at(root / "bare" / "a.cpp"), "is checking this code"),
       "nothing configured anywhere is reported");
    ok(floor_at(root / "good" / "a.rs").empty(), "a language metal has no floor for is silent");
    ok(floor_at(root / "good" / "notes.md").empty(), "docs are not source");
    ok(floor_at("/tmp/one_off.cpp").empty(), "throwaway glue in /tmp is exempt");
    ok(floor_at("/home/u/proj/scratchpad/probe.cpp").empty(), "a scratchpad is not a project");
    ok(floor_at(root / "good" / "build" / "gen.cpp").empty(), "build output is not ours");

    tfs::remove_all(root, ec);
    return 0;
}

std::string choose_at(const tfs::path& p) {
    return choose_check(ev("PreToolUse", p.string().c_str(), "")).value_or("");
}

int rule2(const tfs::path& root) {
    std::error_code ec;
    tfs::remove_all(root, ec);
    put(root / "fresh" / ".git", "");           // a project root with nothing in it yet
    put(root / "settled" / ".git", "");
    put(root / "settled" / "x.py", "print(1)\n");  // language already chosen here
    put(root / "onlybuild" / ".git", "");
    put(root / "onlybuild" / "build.sh", "c++ -std=c++20 main.cpp\n");

    const std::string frame = choose_at(root / "fresh" / "a.py");
    ok(has(frame, "\"allow\""), "the choice is offered, never denied");
    ok(has(frame, "Python"), "the language being chosen is named");
    ok(has(frame, "C++"), "the alternatives carry their tradeoffs");
    ok(has(frame, "excessive work for nothing"), "overkill has an explicit escape");

    ok(choose_at(root / "settled" / "y.py").empty(), "a sibling source file settles it");
    ok(has(choose_at(root / "onlybuild" / "main.cpp"), "C++"),
       "a build script does not settle the language");

    ok(choose_at(root / "fresh" / "notes.md").empty(), "docs do not choose a language");
    ok(choose_at("/tmp/x/one_off.py").empty(), "throwaway glue chooses nothing");
    ok(choose_check(ev("PostToolUse", (root / "fresh" / "b.py").string().c_str(), "")).has_value()
           == false,
       "the question belongs before the write, not after");

    tfs::remove_all(root, ec);
    return 0;
}

int selftest() {
    // Rule 1's message assertions call pre() repeatedly on the same path, which is
    // exactly what the deny streak escalates on. The streak has its own block below that
    // opts back in; everything else wants the first-refusal text.
    setenv("METAL_NO_STAMP", "1", 1);

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

    // Test files get 500/400, because a suite has no parse/emit seam to cut along.
    ok(pre("/p/src/selftest/offload.cpp", lines(350)).empty(),
       "350 lines in a selftest/ dir is silence, where 350 in src/ is a deny");
    ok(has(pre("/p/src/offload.cpp", lines(350)), "\"deny\""),
       "...and the ordinary limit is untouched by the carve-out");
    ok(has(pre("/p/tests/big.py", lines(450)), "still slack"),
       "a test file warns at 400");
    ok(has(pre("/p/tests/huge.py", lines(550)), "\"deny\""),
       "and is still denied past 500 - looser, not exempt");
    ok(has(pre("/p/tests/huge.py", lines(550)), "test files"),
       "the deny explains the test limit, not the parse/emit seam that does not exist here");

    // Escalation. Measured motivation: one file drew 96 denials across 6 sessions, 32 in a
    // single session, every attempt 5,531-5,698 lines against an unchanging message.
    {
        const std::string first = repeat_advice("/p/src/web_dashboard.py", 5500, 1);
        const std::string later = repeat_advice("/p/src/web_dashboard.py", 5500, 4);
        ok(has(first, "still 5500 lines"), "the second refusal restates the size, briefly");
        ok(!has(first, "Make a directory named after the file"),
           "and drops the full split lecture it already gave once");
        ok(has(later, "refused 5 times today"), "the fifth counts out loud");
        ok(has(later, "use Edit, not Write"),
           "and names the way out the first message never mentions");
        ok(has(later, "web_dashboard/"), "including the directory to create, by name");
        ok(first != later, "the second and fifth refusals are not the same text");
    }

    // The streak is per path, expires, and never fires on a first denial.
    unsetenv("METAL_NO_STAMP");  // the streak is the behaviour under test here
    {
        const char* p1 = "/p/streak_a.py";
        const char* p2 = "/p/streak_b.py";
        metal::sfs::remove(metal::deny_record_for(p1));
        metal::sfs::remove(metal::deny_record_for(p2));
        ok(metal::deny_streak(p1) == 0, "a path never denied has no streak");
        metal::record_deny(p1);
        ok(metal::deny_streak(p1) == 1, "recording a denial counts it");
        ok(metal::deny_streak(p2) == 0, "and does not leak onto another file");
        metal::record_deny(p1);
        ok(metal::deny_streak(p1) == 2, "a second denial accumulates");
        // A record older than the window is not a grudge.
        std::ofstream(metal::deny_record_for(p1), std::ios::binary) << "9 1";
        ok(metal::deny_streak(p1) == 0, "a streak from last month has expired");
        metal::sfs::remove(metal::deny_record_for(p1));
        setenv("METAL_NO_STAMP", "1", 1);
    }

    // Every way a file says it is a test.
    ok(metal::is_test("/p/selftest/x.cpp"), "selftest/ dir");
    ok(metal::is_test("/p/tests/x.rs"), "tests/ dir");
    ok(metal::is_test("/p/__tests__/x.ts"), "__tests__/ dir");
    ok(metal::is_test("/p/spec/x.rb"), "spec/ dir");
    ok(metal::is_test("/p/a/test_thing.py"), "test_ prefix");
    ok(metal::is_test("/p/a/thing_test.go"), "_test. suffix");
    ok(metal::is_test("/p/a/thing.test.ts"), ".test. infix");
    ok(metal::is_test("/p/a/thing.spec.js"), ".spec. infix");
    ok(!metal::is_test("/p/src/latest.cpp"), "'latest' is not a test");
    ok(!metal::is_test("/p/src/contest.c"), "nor 'contest'");
    ok(!metal::is_test("/p/protests/x.c"), "nor a dir merely containing 'test'");

    // rules 2 and 3, against real files on disk
    setenv("METAL_NO_STAMP", "1", 1);  // the stamp is a throttle, not behaviour under test
    rule2(tfs::current_path() / ".metal-selftest-2");
    rule3(tfs::current_path() / ".metal-selftest");

    std::printf("ok (%d checks)\n", checks);
    return 0;
}

}  // namespace
