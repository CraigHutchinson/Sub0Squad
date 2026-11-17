#pragma once
// Internal JSON helpers (simdjson stays out of public headers). Writes use simdjson's string_builder with automatic commas;
// reads use the on-demand forward parser with exceptions contained in the .cpp files that include this.
#include <simdjson.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace sub0squad::json {

/// Comma-managing writer over simdjson::builder::string_builder (one reusable buffer, no per-call heap in steady state).
class Writer
{
public:
    Writer() : buffer_(1024) {}
    Writer& beginObject() { sep(); buffer_.append('{'); first_.push_back(true); return *this; }
    Writer& endObject() { buffer_.append('}'); first_.pop_back(); return *this; }
    Writer& beginArray() { sep(); buffer_.append('['); first_.push_back(true); return *this; }
    Writer& endArray() { buffer_.append(']'); first_.pop_back(); return *this; }
    Writer& key(std::string_view k) { sep(); buffer_.escape_and_append_with_quotes(k); buffer_.append(':'); pendingKey_ = true; return *this; }
    Writer& str(std::string_view v) { sep(); buffer_.escape_and_append_with_quotes(v); return *this; }
    Writer& num(std::int64_t v) { sep(); buffer_.append(v); return *this; }
    Writer& num(double v) { sep(); buffer_.append(v); return *this; }
    Writer& boolean(bool v) { sep(); buffer_.append(v); return *this; }
    Writer& null() { sep(); buffer_.append_null(); return *this; }
    Writer& raw(std::string_view v) { sep(); buffer_.append_raw(v); return *this; }
    Writer& kv(std::string_view k, std::string_view v) { return key(k).str(v); }
    Writer& kv(std::string_view k, const std::string& v) { return key(k).str(v); }
    Writer& kv(std::string_view k, const char* v) { return key(k).str(v); }
    Writer& kv(std::string_view k, std::int64_t v) { return key(k).num(v); }
    Writer& kv(std::string_view k, int v) { return key(k).num(static_cast<std::int64_t>(v)); }
    Writer& kv(std::string_view k, double v) { return key(k).num(v); }
    Writer& kvBool(std::string_view k, bool v) { return key(k).boolean(v); }
    Writer& kvRaw(std::string_view k, std::string_view v) { return key(k).raw(v); }

    /// The text so far (valid until the next write or clear()).
    [[nodiscard]] std::string_view view() { std::string_view v; if (buffer_.view().get(v)) return {}; return v; }   // GCC ignores a (void) cast on simdjson's [[nodiscard]]
    [[nodiscard]] std::string take() { return std::string{view()}; }
    void clear() { buffer_.clear(); first_.clear(); pendingKey_ = false; }

private:
    void sep()
    {
        if (pendingKey_) { pendingKey_ = false; return; }
        if (first_.empty()) return;
        if (first_.back()) first_.back() = false; else buffer_.append(',');
    }
    simdjson::builder::string_builder buffer_;
    std::vector<bool> first_;
    bool pendingKey_ = false;
};

/// A parser borrowed from a process-wide pool and returned when it goes out of scope, so parsers (and their grown buffers) are reused without
/// thread_local: MinGW-w64 runs thread_local destructors after the thread's TLS storage is freed, which crashed threads that had parsed JSON.
class PooledParser
{
public:
    PooledParser()
    {
        std::lock_guard lock{pool().mutex};
        if (!pool().free.empty()) { parser_ = std::move(pool().free.back()); pool().free.pop_back(); }
        else parser_ = std::make_unique<simdjson::ondemand::parser>();
    }
    ~PooledParser()
    {
        std::lock_guard lock{pool().mutex};
        pool().free.push_back(std::move(parser_));
    }
    PooledParser(const PooledParser&) = delete;
    PooledParser& operator=(const PooledParser&) = delete;
    simdjson::ondemand::parser& operator*() { return *parser_; }

private:
    struct Pool
    {
        std::mutex mutex;
        std::vector<std::unique_ptr<simdjson::ondemand::parser>> free;   ///< at most the peak number of parses at once
    };
    static Pool& pool() { static Pool p; return p; }
    std::unique_ptr<simdjson::ondemand::parser> parser_;
};

/// Parse `text` and call `fn(object)` for the top-level object. Throws simdjson::simdjson_error on malformed input.
template <class F>
void withObject(std::string_view text, F&& fn)
{
    PooledParser parser;
    simdjson::padded_string padded{text};
    auto doc = (*parser).iterate(padded);
    simdjson::ondemand::object obj = doc.get_object();
    fn(obj);
}

/// Key of the current object field as a plain string_view (valid until the field is advanced).
template <class F>
std::string_view keyOf(F& field) { std::string_view k = field.unescaped_key(); return k; }

template <class V>
std::string toString(V&& v) { std::string_view sv = v.get_string(); return std::string{sv}; }

}  // namespace sub0squad::json
