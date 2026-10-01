/**
 * @file test_config_input_records.cpp
 * @brief Check shared raw-input and syntax rejection before configuration resolution.
 */
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include "io/ConfigParser.h"

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Action>
void rejects(Action action, const std::string& code) {
    try { action(); }
    catch (const ConfigValueError& error) {
        require(error.code == code, "wrong error code"); return;
    }
    throw std::runtime_error("invalid input was accepted");
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass shared candidate fixture directory");
        const std::string fixtures = argv[1];
        ConfigParser parser;
        std::ifstream syntax(fixtures + "/syntax-errors.par", std::ios::binary);
        require(syntax.good(), "missing shared syntax fixture");
        parser.Read(syntax, "stdin");
        require(parser.Diagnostics().size() == 3, "aggregate global errors and duplicate");
        require(parser.GetInt("nblockx2", 9) == 0, "explicit zero lost");
        require(!parser.GetBool("use_burn", true), "explicit false lost");
        rejects([&] { parser.GetDouble("cfl", 0.8); }, "INVALID_NUMBER");
        require(parser.HasKey("solver"), "duplicate became missing");
        const auto& locations = parser.Locations("solver");
        require(locations.size() == 2 && locations[0].line == 4 && locations[1].line == 5,
                "both duplicate positions required");
        require(locations[0].column == 9 && locations[0].raw_value == " HLLC",
                "raw byte position or whitespace lost");
        rejects([&] { parser.GetString("solver", "fallback"); }, "DUPLICATE_PARAMETER");
        rejects([&] { parser.GetAllParams(); }, "MALFORMED_LINE");
        try {
            parser.ThrowIfInvalid();
            throw std::runtime_error("runtime accepted diagnosed syntax");
        } catch (const ConfigInputError& error) {
            require(error.diagnostics.size() == 3, "runtime lost aggregated evidence");
        }
        std::istringstream repeated("a=1\na=2\na=3\n");
        parser.Read(repeated);
        require(parser.Locations("a").size() == 3, "third duplicate lost");
        rejects([&] { parser.GetInt("a", 7); }, "DUPLICATE_PARAMETER");
        rejects([&] { parser.GetBool("a", false); }, "DUPLICATE_PARAMETER");
        rejects([&] { parser.GetDouble("a", 7); }, "DUPLICATE_PARAMETER");

        const std::string text = "# comment\r\n\xc3\xa9 = 0 # suffix\r\nempty =\r\nlast=false";
        std::istringstream exact(text);
        parser.Load(exact, "utf8.par");
        require(parser.InputText() == text, "raw bytes changed");
        require(parser.Locations("\xc3\xa9")[0].column == 5, "column must count UTF-8 bytes");
        require(parser.Locations("\xc3\xa9")[0].end_column == 8, "end must be exclusive");
        require(parser.GetInt("\xc3\xa9", 1) == 0, "UTF-8 key inaccessible");
        require(parser.HasKey("empty") && parser.GetString("empty", "fallback").empty(),
                "empty string is present");
        require(!parser.GetBool("last", true), "last line without LF lost");
        require(!parser.HasKey("solver") && parser.Diagnostics().empty(), "reload retained old state");

        std::istringstream invalid("bad line\n = empty\nx=1\nx=2\n");
        rejects([&] { parser.Load(invalid); }, "MALFORMED_LINE");
        std::istringstream numeric("i=1.5\nb=0\nf=nan\n");
        parser.Load(numeric);
        rejects([&] { parser.GetInt("i", 1); }, "INVALID_INTEGER");
        rejects([&] { parser.GetBool("b", false); }, "INVALID_BOOLEAN");
        rejects([&] { parser.GetDouble("f", 0); }, "INVALID_NUMBER");

        std::ifstream valid(fixtures + "/sod-valid.par");
        require(valid.good() && parser.Load(valid), "valid shared input rejected");
        require(parser.GetString("solver", "") == "HLLC", "valid value changed");
        require(parser.GetDouble("rho_right", 0) == 0.125, "primitive input changed");
        std::istringstream empty;
        parser.Load(empty);
        require(!parser.HasKey("use_burn") && parser.Locations("use_burn").empty(),
                "missing switch acquired a source location");
        require(parser.GetAllParams().empty(), "empty read retained prior values");
        std::cout << "PASS: shared configuration input records and rejection\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
