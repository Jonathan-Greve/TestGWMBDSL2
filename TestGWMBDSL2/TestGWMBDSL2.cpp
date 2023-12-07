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

bool run_test(const parser& p, const std::string& input, int expected, std::unordered_map<std::string, FileVersion>& fileVersions) {
    int val = 0;
    bool success = p.parse(input, val);

    if (!success) {
        std::cout << "Parsing failed for input: " << input << std::endl;
        return false;
    }

    if (val != expected) {
        std::cout << "Test failed for input: " << input << ". Expected: " << expected << ", Got: " << val << std::endl;
        return false;
    }

    std::cout << "Test passed for input: " << input << std::endl;
    return true;
}

int main() {
    // Define the grammar
    auto grammar = R"(
    EXPR          <- OR_OP / NOT_OP
    NOT_OP        <- 'not' OR_OP
    OR_OP         <- AND_OP ('or' AND_OP)*
    AND_OP        <- PRIMARY ('and' PRIMARY)*
    PRIMARY       <- ('(' EXPR ')' / EXISTS / COMP / COMPARE_TYPE) WHITESPACE
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

    // Load the grammar
    auto ok = parser.load_grammar(grammar);
    assert(ok); // Ensure the grammar is loaded correctly

    // Sample data model for file versions
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
            // Only one element, return it directly
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
            // Only one element, return it directly
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
        case 2: // '>'
            return static_cast<int>(left > right);
        case 3: // '<'
            return static_cast<int>(left < right);
        case 4: // '>='
            return static_cast<int>(left >= right);
        case 5: // '<='
            return static_cast<int>(left <= right);
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

    // Define some test cases
    std::vector<std::pair<std::string, int>> test_cases = {
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

        {"not exists(hash0) or exists(hash0)", 0},
        {"not (exists(hash0) or exists(hash0))", 0},
        {"(not exists(hash0)) or exists(hash0)", 1},
    };

    // Run the tests
    bool all_passed = true;
    for (const auto& test : test_cases) {
        bool result = run_test(parser, test.first, test.second, fileVersions);
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