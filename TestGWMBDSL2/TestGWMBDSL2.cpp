#include <iostream>
#include "peglib.h"
#include <unordered_map>
#include <string>

using namespace peg;

struct FileVersion {
    bool exists;
    int hash;
    int size;
};

struct TestCase {
    std::string input;
    int expected;
    bool expect_parse_success;

    TestCase(std::string i, int e, bool eps = true)
        : input(std::move(i)), expected(e), expect_parse_success(eps) {}
};


bool run_test(parser& p, const std::string& input, int expected, std::unordered_map<std::string, FileVersion>& fileVersions, bool expect_parse_success) {
    int val = 0;

    if (expect_parse_success) {
        p.set_logger([](size_t line, size_t col, const std::string& msg, const std::string& rule) {
            std::cerr << line << ":" << col << ": " << msg << " in rule: " << rule << "\n";
            });
    }
    else {
        p.set_logger([](size_t line, size_t col, const std::string& msg, const std::string& rule) {
            });
    }

    bool success = p.parse(input, val);

    if (!success) {
        if (expect_parse_success) {
            std::cout << "Unexpected parsing failure. Test failed for input: " << input << std::endl;
            return false;
        }
    }

    if (val != expected && expect_parse_success) {
        std::cout << "Test failed for input: " << input << ". Expected: " << expected << ", Got: " << val << std::endl;
        return false;
    }

    std::cout << "Test passed for input: " << input << std::endl;
    return true;
}

int main() {
    // Define the grammar
    auto grammar = R"(
    EXPR          <- OR_OP
    OR_OP         <- AND_OP ('or' AND_OP)*
    AND_OP        <- PRIMARY ('and' PRIMARY)*
    PRIMARY       <- (NOT_OP / '(' EXPR ')' / EXISTS / COMP / COMPARE_TYPE) WHITESPACE
    NOT_OP        <- 'not' PRIMARY
    EXISTS        <- 'exists' '(' HASH Number (',' HASH Number)* ')'
    COMP          <- COMPARE_TYPE COMP_OP PRIMARY
    COMP_OP       <- '==' / '!=' / '>=' / '<=' / '>' / '<'
    COMPARE_TYPE  <- HASH Number / SIZE Number / Number
    ~HASH         <- 'hash'
    ~SIZE         <- 'size'
    Number        <- < [0-9]+ >
    ~WHITESPACE   <- SPACE
    ~SPACE        <- (' ' / '\t')*
    %whitespace   <- [ \t]*
)";

    // Create a parser
    parser parser(grammar);
    parser.enable_packrat_parsing();

    parser.set_logger([](size_t line, size_t col, const std::string& msg, const std::string& rule) {
        std::cerr << line << ":" << col << ": " << msg << " in rule: " << rule << "\n";
        });

    auto ok = parser.load_grammar(grammar);
    assert(ok);

    // Sample data for tests
    std::unordered_map<std::string, FileVersion> fileVersions = {
        {"v0", {true, 0, 150}},  // v0 exists with size 150
        {"v1", {false, 1, 0}},   // v1 does not exist
        {"v2", {true, 2, 200}}   // v2 exists with size 200
    };

    std::set<int> hashes;

    // Define semantic actions
    parser["COMPARE_TYPE"] = [&](const SemanticValues& sv) {
        auto num = any_cast<int>(sv[0]);
        auto val = "v" + std::to_string(num);
        if (fileVersions.contains(val)) {
            switch (sv.choice())
            {
            case 0:
                return fileVersions[val].hash;
            case 1:
                return fileVersions[val].size;
            default:
                break;
            }
        }

        return num;
        };

    parser["NOT_OP"] = [&](const SemanticValues& sv) {
        return static_cast<int>(!static_cast<bool>(any_cast<int>(sv[0])));
        };

    parser["OR_OP"] = [&](const SemanticValues& sv) {
        if (sv.size() == 1) {
            return any_cast<int>(sv[0]);
        }

        bool result = false;
        for (const auto& value : sv) {
            int val = any_cast<int>(value);
            result = result || (val != 0);
            if (result) break; // Short-circuit evaluation
        }
        return static_cast<int>(result);
        };

    parser["AND_OP"] = [&](const SemanticValues& sv) {
        if (sv.size() == 1) {
            return any_cast<int>(sv[0]);
        }

        bool result = true;
        for (const auto& value : sv) {
            int val = any_cast<int>(value);
            result = result && (val != 0);
            if (!result) break; // Short-circuit evaluation
        }
        return static_cast<int>(result);
        };

    parser["EXISTS"] = [&](const SemanticValues& sv) {
        bool allExist = true;

        for (const auto& value : sv) {
            int num = any_cast<int>(value);
            auto val = "v" + std::to_string(num);
            if (!fileVersions.contains(val)) {
                allExist = false;
                break;
            }
        }

        return static_cast<int>(allExist);
        };

    parser["COMP"] = [&](const SemanticValues& sv) {
        auto left = any_cast<int>(sv[0]);
        auto right = any_cast<int>(sv[2]);
        auto op_choice = any_cast<int>(sv[1]);

        switch (op_choice) {
        case 0: // '=='
            return static_cast<int>(left == right);
        case 1: // '!='
            return static_cast<int>(left != right);
        case 2: // '>='
            return static_cast<int>(left >= right);
        case 3: // '<='
            return static_cast<int>(left <= right);
        case 4: // '>'
            return static_cast<int>(left > right);
        case 5: // '<'
            return static_cast<int>(left < right);
        default:
            break;
        }
        return 0;
        };

    parser["COMP_OP"] = [&](const SemanticValues& sv) {
        return static_cast<int>(sv.choice());
        };

    parser["PRIMARY"] = [&](const SemanticValues& sv) {
        const auto out = any_cast<int>(sv[0]);
        return out;
        };

    parser["Number"] = [](const SemanticValues& sv) {
        const auto number = sv.token_to_number<int>();
        return number;
        };

    std::vector<TestCase> test_cases = {
        {"hash0 == hash0", 1},
        {"hash0 == hash1", 0},
        {"hash0 == hash2", 0},

        {"hash1 == hash0", 0},
        {"hash1 == hash1", 1},
        {"hash1 == hash2", 0},

        {"hash2 == hash0", 0},
        {"hash2 == hash1", 0},
        {"hash2 == hash2", 1},

        {"size0 == 150", 1},
        {"size1 == 0", 1},
        {"size2 == 200", 1},

        {"size0 == 1", 0},
        {"size1 == 1", 0},
        {"size2 == 1", 0},

        {"size2 > size1", 1},
        {"size2 < size1", 0},
        {"size2 <= size1", 0},

        {"(hash2 == hash1 or hash2 == hash0)", 0},
        {"(hash2 == hash1 or hash2 == hash0) or size1 == 0", 1},
        {"(hash2 == hash1 or hash2 == hash0) or size1 == 0 and size1 == 1", 0},
        {"(hash2 == hash1 or hash2 == hash0) or size1 == 0 and size1 == 0", 1},
        {"(hash2 == hash1 or hash2 == hash0) or size1 == 0 and size0 == 150", 1},

        {"hash2 == hash1 or hash2 == hash0", 0},
        {"hash2 == hash1 or hash2 == hash0 or size1 == 0", 1},
        {"hash2 == hash1 or hash2 == hash0 or size1 == 0 and size1 == 1", 0},
        {"hash2 == hash1 or hash2 == hash0 or size1 == 0 and size1 == 0", 1},
        {"hash2 == hash1 or hash2 == hash0 or size1 == 0 and size0 == 150", 1},
        {"1 or 2", 1},
        {"0 or 0", 0},
        {"0 and 0", 0},
        {"1 and 0", 0},
        {"1 and 1", 1},
        {"1 and 2", 1},
        {"exists(hash0)", 1},
        {"exists(hash1)", 1},
        {"exists(hash2)", 1},
        {"exists(hash3)", 0},

        {"not exists(hash0)", 0},
        {"not exists(hash1)", 0},
        {"not exists(hash2)", 0},
        {"not exists(hash3)", 1},

        {"not exists(hash0) or exists(hash0)", 1},
        {"not (exists(hash0) or exists(hash0))", 0},
        {"(not exists(hash0)) or exists(hash0)", 1},

        {"hash2 == hash1 or not hash2 == hash0", 1},
        {"hash2 == hash1 not or hash2 == hash0", 1, false},

        {"hash0 != hash1", 1},
        {"size1 != 150", 1},
        {"hash0 != size0", 1},
        {"size2 != hash2", 1},

        {"size1 > size0", 0},
        {"size2 > 100", 1},
        {"150 < size2", 1},
        {"size0 < 150", 0},
        {"size1 < 200", 1},

        {"size0 >= 150", 1},
        {"size1 <= 0", 1},
        {"200 >= size2", 1},
        {"size2 <= 300", 1},
        {"100 <= size1", 0},

        {"size0 == 150 and size1 == 0", 1},
        {"hash1 == hash0 or size1 < size2", 1},
        {"hash1 == hash0 or size1 > size2", 0},
        {"size2 > size1 and hash2 != hash1", 1},
        {"(size0 == 150 or size1 == 0) and hash2", 1},
        {"hash0 and size0 == 150", 0},

        {"not size0 == 150", 0},
        {"not (hash1 == hash2)", 1},
        {"not size2 < size1", 1},
        {"not (size2 > 100 and size1 == 0)", 0},
        {"not (size0 < 150 or size2 == 200)", 0},
        {"not hash0 != size1", 1},

        {"exists(hash0, hash1)", 1},
        {"exists(hash3, hash1)", 0},
        {"not exists(hash3)", 1},
        {"exists(hash2) and hash2 == 2", 1},
        {"exists(hash1) or size2 > 200", 1},

        {"(size0 == 150 or size1 < size2) and not hash1", 0},
        {"not (hash2 != hash1 and size1 >= 0)", 0},
        {"(exists(hash0, hash1) or size2 < 300) and size0", 1},
        {"not (size2 <= size0 or hash0 == hash1)", 1},
        {"(size1 == 0 and not size0 == 150) or hash2", 1},
        {"(size1 == 0 and not size0 == 150)", 0 },

        {"not (not size0 == 150)", 1},
        {"not not size0 == 150", 1 },
        {"not not not size0 == 150", 0 },
        { "not (not (not (size0 == 150)))", 0 },
        {"(not (size1 > size0) and size2)", 1},
        {"not (exists(hash3) or not size2 >= 200)", 1},
        {"(exists(hash0) and not (size1 or not hash2))", 1},
        {"(not (hash1 == hash0) and not (size2 < size1))", 1}
    };

    // Run the tests
    bool all_passed = true;
    for (const auto& test : test_cases) {
        bool result = run_test(parser, test.input, test.expected, fileVersions, test.expect_parse_success);
        all_passed = all_passed && result;
    }

    if (all_passed) {
        std::cout << "All tests passed!" << std::endl;
    }
    else {
        std::cout << "Some tests failed." << std::endl;
    }

    return 0;
}
