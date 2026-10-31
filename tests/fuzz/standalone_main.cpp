// A stand-in for libFuzzer where it isn't available (e.g. Apple's Clang):
// runs the target on the files given, or on random and mutated inputs for
// FUZZ_ITERATIONS rounds (default 20000). Much weaker than libFuzzer, but
// it keeps the targets building and catches the obvious.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
extern "C" __attribute__((weak)) int LLVMFuzzerInitialize(int *argc, char ***argv);

int main(int argc, char **argv)
{
    if (LLVMFuzzerInitialize)
        LLVMFuzzerInitialize(&argc, &argv);
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            std::ifstream in(argv[i], std::ios::binary);
            const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
            LLVMFuzzerTestOneInput(data.data(), data.size());
        }
        return 0;
    }

    const char *env = std::getenv("FUZZ_ITERATIONS");
    const long iterations = env ? std::strtol(env, nullptr, 10) : 20000;
    std::mt19937 rng(12345);
    // Pieces that matter to the targets, mixed into random bytes.
    const std::vector<std::string> tokens = {
        "\r\n", "\n", "\r", std::string(1, '\0'), "\xc3", "\xc3\xa9", "\xff", "\xe2\x82\xac",
        "sftp://", "user@", ":22", "/..", "../", "/", "~", "[::1]", "%2F", "%00", ":pw@",
        "<script>", "</script>", "<style>", "<!--", "-->", "/*", "*/", "//", "\"", "'", "`",
        "#include", "<", ">", "=", "\\", "\x1b[31m", "‮",
        // Markdown, Python, Rust and LaTeX (2.5).
        "```", "~~~", "**", "_", "[", "](", "\"\"\"", "'''", "r#\"", "\"#", "'a", "#[", "$", "$$",
        "\\begin{", "\\end{", "itemize}", "equation}", "tabular}{l|c}", "verbatim}", "\\item",
        "\\frac{", "\\newcommand{\\x}[1]{#1#1}", "\\x", "{", "}", "&", "\\\\", "%",
        "\\verb|", "\\section{", "^", "\\label{a}", "\\ref{a}", "\\def\\y{\\y}"};
    std::vector<uint8_t> input;
    for (long i = 0; i < iterations; ++i) {
        if (i % 50 == 0)
            input.clear();
        const int edits = 1 + int(rng() % 8);
        for (int e = 0; e < edits; ++e) {
            if (rng() % 3 == 0) {
                const std::string &t = tokens[rng() % tokens.size()];
                input.insert(input.begin() + long(input.empty() ? 0 : rng() % input.size()), t.begin(), t.end());
            } else if (rng() % 4 == 0 && !input.empty()) {
                input.erase(input.begin() + long(rng() % input.size()));
            } else {
                input.insert(input.begin() + long(input.empty() ? 0 : rng() % input.size()), uint8_t(rng()));
            }
        }
        if (input.size() > 4096)
            input.resize(4096);
        LLVMFuzzerTestOneInput(input.data(), input.size());
    }
    std::printf("%ld inputs, no failures\n", iterations);
    return 0;
}
