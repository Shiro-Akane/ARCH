/**
 * @file TabularBaryonSource.cpp
 * @brief Decode a shared baryon ASCII schema without adding physical components.
 *
 * Source definitions: EOS2/EOS4 author guides, section VI, at
 * https://user.numazu-ct.ac.jp/~sumi/eos/ . Both use rho_B=m_u*n_B,
 * E relative to 931.494 MeV and F relative to 938 MeV per baryon.
 * Their printed density/nB pairs use the fixed rounded 1.66054e-24 g
 * convention, not the current CODATA mass. Retaining that fixed convention
 * avoids turning a constant rest-energy reference into a density function.
 */
#include "TabularBaryonSource.h"

#include "physics/constant/PhysicalConstants.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace tabular_eos::source {
namespace {
struct Number {
    double value = 0.0;
    double half_printed_unit = 0.0;
};
using Row = std::array<Number, 16>;

[[noreturn]] void malformed(std::size_t line, const std::string& reason)
{
    throw std::runtime_error("Baryon ASCII table line " + std::to_string(line) + ": " + reason);
}

std::vector<std::string> words(const std::string& line)
{
    std::istringstream input(line);
    std::vector<std::string> result;
    for (std::string word; input >> word;) result.push_back(std::move(word));
    return result;
}

bool next_content(std::istream& input, std::size_t& line_number, std::vector<std::string>& tokens)
{
    for (std::string line; std::getline(input, line);) {
        ++line_number;
        tokens = words(line);
        if (!tokens.empty()) return true;
    }
    if (!input.eof()) malformed(line_number, "input read failed");
    return false;
}

bool separator(const std::vector<std::string>& tokens)
{
    return tokens.size() == 1 && tokens[0] == "cccccccccccc";
}

bool temperature_label(const std::vector<std::string>& tokens)
{
    return tokens.size() == 2
        && ((tokens[0] == "Log10(T)" && tokens[1] == "T")
            || (tokens[0] == "Log10(Temp)" && tokens[1] == "Temp"));
}

Number number(std::string token, std::size_t line)
{
    for (char& c : token) if (c == 'd' || c == 'D') c = 'E';
    std::string_view text(token);
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    Number result;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result.value,
        std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()
        || !std::isfinite(result.value))
        malformed(line, "expected a finite decimal number");
    const auto exponent_at = text.find_first_of("eE");
    int exponent = 0;
    if (exponent_at != std::string_view::npos) {
        auto exponent_text = text.substr(exponent_at + 1);
        if (!exponent_text.empty() && exponent_text.front() == '+') exponent_text.remove_prefix(1);
        const auto parsed_exponent = std::from_chars(exponent_text.data(),
            exponent_text.data() + exponent_text.size(), exponent);
        if (parsed_exponent.ec != std::errc{}) malformed(line, "invalid decimal exponent");
    }
    const auto mantissa = text.substr(0, exponent_at);
    const auto point = mantissa.find('.');
    const int decimals = point == std::string_view::npos ? 0
        : static_cast<int>(mantissa.size() - point - 1);
    result.half_printed_unit = 0.5 * std::pow(10.0, exponent - decimals);
    if (!std::isfinite(result.half_printed_unit)) malformed(line, "invalid printed precision");
    return result;
}

bool matches_printed(const Number& printed, double expected)
{
    const double arithmetic = 16.0 * std::numeric_limits<double>::epsilon()
        * std::max(std::abs(printed.value), std::abs(expected));
    return std::abs(printed.value - expected) <= printed.half_printed_unit + arithmetic;
}

struct SourceBlocks {
    std::vector<double> log_temperature_mev;
    std::vector<std::vector<Row>> rows;
};

SourceBlocks read_blocks(const std::string& path)
{
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open baryon ASCII table: " + path);
    SourceBlocks blocks;
    std::size_t line = 0;
    std::vector<std::string> tokens;
    bool available = next_content(input, line, tokens);
    while (available) {
        if (!separator(tokens)) malformed(line, "expected temperature-block separator");
        if (!next_content(input, line, tokens) || !temperature_label(tokens))
            malformed(line, "expected logarithmic/linear temperature header");
        if (!next_content(input, line, tokens) || tokens.size() != 2)
            malformed(line, "expected two temperature values");
        const auto log_t = number(tokens[0], line);
        const auto t = number(tokens[1], line);
        const double temperature = std::pow(10.0, log_t.value);
        if (!(t.value > 0.0) || !std::isfinite(temperature) || !(temperature > 0.0)
            || !matches_printed(t, temperature))
            malformed(line, "temperature pair is not a consistent finite-temperature axis");
        if (!blocks.log_temperature_mev.empty()
            && !(log_t.value > blocks.log_temperature_mev.back()))
            malformed(line, "temperature blocks must be strictly increasing");
        blocks.log_temperature_mev.push_back(log_t.value);
        blocks.rows.emplace_back();
        while ((available = next_content(input, line, tokens)) && !separator(tokens)) {
            if (tokens.size() != 16) malformed(line, "expected exactly 16 baryon fields");
            Row row{};
            for (std::size_t i = 0; i < row.size(); ++i) row[i] = number(tokens[i], line);
            blocks.rows.back().push_back(row);
        }
        if (blocks.rows.back().empty()) malformed(line, "empty temperature block");
    }
    if (blocks.rows.size() < 2)
        throw std::runtime_error("Baryon ASCII table requires at least two positive-temperature blocks");
    return blocks;
}
} // namespace

bool is_baryon_ascii_table(const std::string& path)
{
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot inspect baryon ASCII table: " + path);
    std::size_t line = 0;
    std::vector<std::string> tokens;
    return next_content(input, line, tokens) && separator(tokens)
        && next_content(input, line, tokens) && temperature_label(tokens);
}

BaryonTable read_baryon_ascii_table(const std::string& path)
{
    const auto blocks = read_blocks(path);
    const auto& first = blocks.rows.front();
    std::size_t nr = 1;
    while (nr < first.size() && first[nr][0].value > first[nr - 1][0].value) ++nr;
    if (nr < 2 || first.size() % nr != 0 || first.size() / nr < 2)
        throw std::runtime_error("Baryon ASCII table requires a rectangular rho/Ye grid with at least two nodes per axis");
    const std::size_t ny = first.size() / nr, nt = blocks.rows.size();
    if (nt > std::numeric_limits<std::size_t>::max() / first.size())
        throw std::runtime_error("Baryon ASCII table extent overflow");
    const std::size_t count = nt * first.size();
    BaryonTable table;
    for (std::size_t r = 0; r < nr; ++r) {
        table.log_density.push_back(first[r][0].value);
        const double rho = std::pow(10.0, first[r][0].value);
        if (!(rho > 0.0) || !std::isfinite(rho))
            throw std::runtime_error("Baryon ASCII density axis is not finite and positive");
        table.density.push_back(rho);
    }
    for (std::size_t y = 0; y < ny; ++y) {
        const double ye = first[y * nr][2].value;
        if (!(ye > 0.0 && ye <= 1.0)
            || (!table.electron_fraction.empty() && !(ye > table.electron_fraction.back())))
            throw std::runtime_error("Baryon ASCII Ye axis must be strictly increasing in (0,1]");
        table.electron_fraction.push_back(ye);
    }
    constexpr double mev_per_kelvin = arch::constants::statistical::cgs::boltzmann
        / arch::constants::units::erg_per_mev;
    for (const double lt : blocks.log_temperature_mev) {
        const double log_kelvin = lt - std::log10(mev_per_kelvin);
        const double kelvin = std::pow(10.0, log_kelvin);
        if (!std::isfinite(kelvin) || !(kelvin > 0.0))
            throw std::runtime_error("Baryon ASCII temperature conversion overflow");
        table.log_temperature.push_back(log_kelvin);
        table.temperature.push_back(kelvin);
    }
    for (auto* field : {&table.pressure, &table.energy, &table.free_energy, &table.entropy,
            &table.source_number_density_fm3, &table.source_electron_fraction,
            &table.source_pressure_mev_fm3, &table.source_energy_mev,
            &table.source_free_energy_mev, &table.source_entropy_kb, &table.source_valid})
        field->resize(count);
    const double energy_per_mass = arch::constants::units::erg_per_mev / table.baryon_mass_g;
    const double entropy_per_mass = arch::constants::statistical::cgs::boltzmann / table.baryon_mass_g;
    constexpr double pressure_to_cgs = arch::constants::units::erg_per_mev * 1.0e39;
    for (std::size_t t = 0; t < nt; ++t) {
        if (blocks.rows[t].size() != nr * ny)
            throw std::runtime_error("Baryon ASCII temperature blocks have different extents");
        for (std::size_t y = 0; y < ny; ++y) for (std::size_t r = 0; r < nr; ++r) {
            const auto& row = blocks.rows[t][y * nr + r];
            if (row[0].value != table.log_density[r])
                throw std::runtime_error("Baryon ASCII density coordinates do not form a common tensor grid");
            const std::size_t i = (r * nt + t) * ny + y;
            table.source_number_density_fm3[i] = row[1].value;
            table.source_electron_fraction[i] = row[2].value;
            table.source_free_energy_mev[i] = row[3].value;
            table.source_energy_mev[i] = row[4].value;
            table.source_entropy_kb[i] = row[5].value;
            table.source_pressure_mev_fm3[i] = row[13].value;
            table.pressure[i] = row[13].value * pressure_to_cgs;
            table.energy[i] = row[4].value * energy_per_mass;
            table.free_energy[i] = (row[3].value + table.free_energy_alignment_mev) * energy_per_mass;
            table.entropy[i] = row[5].value * entropy_per_mass;
            for (double value : {table.pressure[i], table.energy[i], table.free_energy[i], table.entropy[i]})
                if (!std::isfinite(value)) throw std::runtime_error("Baryon ASCII cgs field conversion overflow");
            const double expected_nb = table.density[r] / table.baryon_mass_g / 1.0e39;
            table.source_valid[i] = row[1].value > 0.0 && row[5].value >= 0.0
                && matches_printed(row[1], expected_nb)
                && matches_printed(row[2], table.electron_fraction[y]) ? 1.0 : 0.0;
        }
    }
    return table;
}
} // namespace tabular_eos::source
