/**
 * @file HelmTableReader.h
 * @brief Host-only buffered decimal reader for the canonical Timmes
 *        helm_table.dat ASCII table.
 *
 * @par Workflow
 * Every read() call performs exactly these steps, in order:
 *  1. skip classic-locale whitespace, refilling the fixed ~64 KiB window
 *     whenever it is exhausted (clean end of input or a hard stream error
 *     stops here);
 *  2. gather exactly one non-whitespace token, parsing it in place when it
 *     lies inside the window and copying the prefix into a growable carry
 *     string only when the token straddles a refill (no whole-file buffer and
 *     no new narrow token length limit);
 *  3. convert that one token with std::from_chars general format; the rarely
 *     reached std::errc::result_out_of_range result falls back to
 *     classic-locale formatted extraction, which is the reference behaviour of
 *     the original std::istream operator>> extractor.
 * The value is assigned only when a whole token converted to a finite double.
 * Every other outcome -- end of input, hard stream error, malformed or
 * incomplete token, nonfinite result -- makes read() return false and leaves
 * the reader in a sticky failed state, exactly like the failed formatted
 * extractor it replaces.
 */
#ifndef HELM_TABLE_READER_H
#define HELM_TABLE_READER_H

#include <charconv>
#include <cmath>
#include <cstddef>
#include <istream>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace helm_eos
{
namespace loader
{

// Host-only buffered reader for the canonical Timmes helm_table.dat text
// file. It reproduces the semantics of the formatted extraction
// (std::istream operator>> for double) that the host EOS owner used before:
// classic-locale leading whitespace is skipped, exactly one token is
// converted per call, and incomplete, malformed or nonfinite tokens fail
// instead of being partially accepted; finite underflow rounds like the
// original extractor while overflow still fails. Unlike per-element formatted
// extraction, the source is consumed in bounded chunks (~64 KiB), so the
// ~60 MB table is never buffered whole and no stream sentry / num_get work is
// paid per value.
class HelmTableReader
{
public:
    // Binds the reader to an already opened byte stream. The caller keeps
    // ownership of the stream and must keep it alive and open while the
    // reader is in use; the reader keeps only its own buffered state.
    explicit HelmTableReader(std::istream &source)
        : source_(source), buffer_(kBufferSize)
    {
    }

    // Extract the next whitespace-separated finite decimal value. Returns
    // false at end of input or when the next token cannot be converted
    // exactly; the value is written only on success and a failure is sticky,
    // mirroring the failed state of the original formatted extractor.
    bool read(double &value)
    {
        if (failed_)
            return false;

        token_carry_.clear();

        // Skip the whitespace that separates tokens, refilling the window
        // whenever it has been fully consumed. Running out of input before
        // any token byte means there is nothing left to extract.
        for (;;)
        {
            while (pos_ < filled_ && is_space(buffer_[pos_]))
                ++pos_;
            if (pos_ < filled_)
                break;
            if (!refill())
            {
                failed_ = true;
                return false;
            }
        }

        // Gather the token. A token that lies inside the current window is
        // parsed in place; only a token split by a refill is copied into the
        // carry string, which then grows for as long as that one token needs.
        bool carried = false;
        for (;;)
        {
            const std::size_t start = pos_;
            while (pos_ < filled_ && !is_space(buffer_[pos_]))
                ++pos_;
            if (carried)
                token_carry_.append(buffer_.data() + start, pos_ - start);
            else
            {
                token_first_ = buffer_.data() + start;
                token_last_ = buffer_.data() + pos_;
            }

            if (pos_ < filled_)
                break; // token ended at a whitespace byte
            if (stream_eof_)
                break; // token ended at end of input

            // The token continues in the next chunk: preserve the prefix
            // before the refill overwrites the window, then carry on.
            if (!carried)
            {
                token_carry_.assign(token_first_, token_last_);
                carried = true;
            }
            if (!refill())
                break;
        }

        // A hard stream error invalidates the token even when a prefix has
        // already been carried: truncated bytes are never published as a
        // valid final token, and read() stays failed from here on.
        if (io_error_)
        {
            failed_ = true;
            return false;
        }

        // A valid token must start at its first byte and consume its last
        // one, so trailing or embedded junk is never dropped silently.
        const char *first = carried ? token_carry_.data() : token_first_;
        const char *last = carried ? token_carry_.data() + token_carry_.size()
                                   : token_last_;
        if (!parse_value(first, last, value))
        {
            failed_ = true;
            return false;
        }
        return true;
    }

private:
    // Streaming chunk size: large enough to amortise stream calls and small
    // enough to stay cache friendly, independent of the input length.
    static constexpr std::size_t kBufferSize = 64 * 1024;

    // Whitespace set of the classic C locale, which is what the default
    // std::istream formatted extraction skips.
    static bool is_space(char byte)
    {
        return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\v' ||
               byte == '\f' || byte == '\r';
    }

    // Convert one complete token with std::from_chars in general
    // (decimal / eE-exponent) format. The from_chars overloads, unlike the
    // formatted extractor, reject a leading '+', so exactly one is removed
    // first; a second sign after it ("+-1", "++1") is malformed for the
    // original extractor as well and stays rejected. The conversion must
    // consume the whole token and yield a finite value, and "-0" keeps its
    // sign because from_chars rounds exactly.
    static bool parse_value(const char *first, const char *last, double &value)
    {
        if (first == last)
            return false; // empty token, or nothing left at end of input

        if (*first == '+')
        {
            ++first;
            if (first == last || *first == '+' || *first == '-')
                return false; // bare '+' or duplicate sign
        }
        else if (*first == '-' && last - first == 1)
        {
            return false; // bare '-'
        }

        double parsed = 0.0;
        const std::from_chars_result result =
            std::from_chars(first, last, parsed, std::chars_format::general);
        if (result.ec == std::errc::result_out_of_range)
            return parse_formatted(first, last, value); // rare underflow/overflow
        if (result.ec != std::errc{} || result.ptr != last)
            return false; // malformed, incomplete exponent, or partial token
        if (!std::isfinite(parsed))
            return false; // inf/nan spellings and nonfinite results

        value = parsed;
        return true;
    }

    // Compatibility path for the rare tokens whose full-token from_chars
    // conversion reported std::errc::result_out_of_range. It runs the original
    // reference conversion (classic-locale formatted extraction, i.e. the same
    // num_get path as the replaced operator>>) so historical results are kept:
    // finite underflow rounds to zero and keeps its sign, while overflow sets
    // failbit, and partial tokens or nonfinite results are still refused.
    static bool parse_formatted(const char *first, const char *last,
                                double &value)
    {
        std::istringstream stream(std::string(first, last));
        stream.imbue(std::locale::classic());
        double parsed = 0.0;
        if (!(stream >> parsed))
            return false; // conversion failed, e.g. overflow sets failbit
        if (stream.peek() != std::char_traits<char>::eof())
            return false; // trailing junk: a partial token is never accepted
        if (!std::isfinite(parsed))
            return false; // nonfinite result is never published
        value = parsed;
        return true;
    }

    // Load the next chunk into the window. Must only be called once the
    // window is fully consumed. A short final read is accepted as data and
    // only the following call reports clean end of input; a hard stream error
    // is recorded in io_error_ so the bytes that failed read returned are
    // never accepted, and stream_eof_ then keeps every later call from
    // retrying, so read() can never loop without making progress.
    bool refill()
    {
        if (stream_eof_)
            return false;

        pos_ = 0;
        filled_ = 0;

        if (source_.bad())
        {
            io_error_ = true;
            stream_eof_ = true;
            return false;
        }
        if (source_.fail())
        {
            // failbit left behind by an earlier short (end-of-file) read or by
            // a pre-failed stream: the byte stream ends here without data.
            stream_eof_ = true;
            return false;
        }

        source_.read(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
        const std::streamsize extracted = source_.gcount();
        if (source_.bad())
        {
            // Hard I/O failure: fail before accepting any gcount byte, so a
            // truncated token can never be published as a valid value.
            io_error_ = true;
            stream_eof_ = true;
            return false;
        }
        if (extracted <= 0)
        {
            stream_eof_ = true; // clean end of input
            return false;
        }

        filled_ = static_cast<std::size_t>(extracted);
        return true;
    }

    std::istream &source_;
    // Fixed-size window, never resized, so the pointers handed to
    // from_chars stay valid until the next refill.
    std::vector<char> buffer_;
    std::size_t pos_ = 0;    // first unconsumed byte in buffer_
    std::size_t filled_ = 0; // number of valid bytes in buffer_
    // Bytes of a token that straddles a refill boundary; grows only as far as
    // that one token needs, so the input length is never materialised.
    std::string token_carry_;
    const char *token_first_ = nullptr; // token start inside buffer_
    const char *token_last_ = nullptr;  // token end inside buffer_
    bool stream_eof_ = false;           // end of input seen (clean or failed)
    bool io_error_ = false;             // hard stream failure, sticky
    bool failed_ = false;               // sticky extraction failure
};

} // namespace loader
} // namespace helm_eos

#endif // HELM_TABLE_READER_H
