#include "TestHarness.h"

#include "maras/foundation/Logging.h"

#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS::Tests {

    namespace {

        struct ActiveTestContext {
            std::size_t assertionsChecked{ 0 };
            std::vector<FailureDiagnostic> failures{};
        };

        ActiveTestContext& CurrentContext() {
            static ActiveTestContext ctx{};
            return ctx;
        }

        void NullTestLogSink(Foundation::LogSeverity, std::string_view) noexcept {}

    }  // namespace

    std::vector<TestCaseEntry>& Registry() {
        static std::vector<TestCaseEntry> entries{};
        return entries;
    }

    void RecordAssertion(
        bool passed,
        std::string_view expr,
        std::string detail,
        std::source_location loc) {
        auto& ctx = CurrentContext();
        ++ctx.assertionsChecked;
        if (!passed) {
            ctx.failures.push_back(FailureDiagnostic{
                .expression = std::string(expr),
                .detail = std::move(detail),
                .file = loc.file_name(),
                .line = loc.line(),
            });
        }
    }

    int RunAllTests(std::string_view filterSubstring) {
        Foundation::SetLogSink(&NullTestLogSink);

        std::size_t passedTests = 0;
        std::size_t failedTests = 0;
        std::size_t skippedTests = 0;
        std::size_t totalAssertions = 0;

        for (const TestCaseEntry& tc : Registry()) {
            const std::string fullName = std::format("{}.{}", tc.suite, tc.name);
            if (!filterSubstring.empty() && fullName.find(filterSubstring) == std::string::npos) {
                ++skippedTests;
                continue;
            }

            auto& ctx = CurrentContext();
            ctx.assertionsChecked = 0;
            ctx.failures.clear();

            try {
                tc.fn();
            } catch (const TestAbortException&) {
                // Expected early abort from MARAS_REQUIRE / MARAS_REQUIRE_EQ
            } catch (const std::exception& ex) {
                ctx.failures.push_back(FailureDiagnostic{
                    .expression = "unhandled std::exception",
                    .detail = ex.what(),
                    .file = "<test>",
                    .line = 0,
                });
            } catch (...) {
                ctx.failures.push_back(FailureDiagnostic{
                    .expression = "unhandled unknown exception",
                    .detail = {},
                    .file = "<test>",
                    .line = 0,
                });
            }

            totalAssertions += ctx.assertionsChecked;

            if (ctx.failures.empty()) {
                ++passedTests;
                std::fprintf(stdout, "[ PASS ] %s (%zu checks)\n", fullName.c_str(), ctx.assertionsChecked);
            } else {
                ++failedTests;
                std::fprintf(stderr, "[ FAIL ] %s (%zu failure(s))\n", fullName.c_str(), ctx.failures.size());
                for (const FailureDiagnostic& f : ctx.failures) {
                    if (f.detail.empty()) {
                        std::fprintf(
                            stderr,
                            "         %s:%u: check failed: %s\n",
                            f.file.c_str(),
                            static_cast<unsigned>(f.line),
                            f.expression.c_str());
                    } else {
                        std::fprintf(
                            stderr,
                            "         %s:%u: check failed: %s\n         -> %s\n",
                            f.file.c_str(),
                            static_cast<unsigned>(f.line),
                            f.expression.c_str(),
                            f.detail.c_str());
                    }
                }
            }
        }

        std::fprintf(
            stdout,
            "\nSummary: %zu passed, %zu failed, %zu skipped (%zu assertions)\n",
            passedTests,
            failedTests,
            skippedTests,
            totalAssertions);

        return failedTests == 0 ? 0 : 1;
    }

}  // namespace MARAS::Tests

int main(int argc, char** argv) {
    const std::string_view filter = (argc > 1 && argv[1] != nullptr) ? std::string_view(argv[1]) : std::string_view{};
    return MARAS::Tests::RunAllTests(filter);
}
