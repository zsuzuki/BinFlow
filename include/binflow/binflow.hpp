#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace binflow {

struct omit_if_default_t {
    explicit omit_if_default_t() = default;
};

inline constexpr omit_if_default_t omit_if_default{};

enum class wire_type : std::uint8_t {
    varint = 0,
    fixed32 = 1,
    fixed64 = 2,
    bytes = 3,
};

struct field_id {
    std::uint32_t value;
};

namespace literals {

consteval field_id operator""_f(unsigned long long value) {
    if (value == 0 || value > 0xffff'ffffull) {
        throw "field id must be in the range 1..4294967295";
    }
    return field_id{static_cast<std::uint32_t>(value)};
}

} // namespace literals

class memory_writer {
public:
    memory_writer() = default;

    explicit memory_writer(std::size_t capacity) {
        reserve(capacity);
    }

    void reserve(std::size_t capacity) {
        data_.reserve(capacity);
    }

    void write(std::span<const std::byte> bytes) {
        data_.insert(data_.end(), bytes.begin(), bytes.end());
    }

    [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept {
        return data_;
    }

    [[nodiscard]] std::vector<std::byte> take_bytes() && {
        return std::move(data_);
    }

private:
    std::vector<std::byte> data_;
};

class fixed_memory_writer {
public:
    fixed_memory_writer(void* data, std::size_t size)
        : data_(static_cast<std::byte*>(data)), size_(size) {
        if (data == nullptr && size != 0) {
            throw std::runtime_error("output buffer is null");
        }
    }

    void write(std::span<const std::byte> bytes) {
        if (remaining() < bytes.size()) {
            throw std::runtime_error("output buffer is too small");
        }
        std::memcpy(data_ + pos_, bytes.data(), bytes.size());
        pos_ += bytes.size();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return pos_;
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return size_;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return size_ - pos_;
    }

private:
    std::byte* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

class file_writer {
public:
    explicit file_writer(const std::string& path)
        : out_(path, std::ios::binary) {
        if (!out_) {
            throw std::runtime_error("failed to open file for writing: " + path);
        }
    }

    void write(std::span<const std::byte> bytes) {
        out_.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out_) {
            throw std::runtime_error("failed to write file");
        }
    }

private:
    std::ofstream out_;
};

class memory_reader {
public:
    explicit memory_reader(std::span<const std::byte> bytes)
        : bytes_(bytes) {}

    [[nodiscard]] bool eof() const noexcept {
        return pos_ == bytes_.size();
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - pos_;
    }

    [[nodiscard]] std::byte read_byte() {
        if (remaining() < 1) {
            throw std::runtime_error("unexpected end of input");
        }
        return bytes_[pos_++];
    }

    [[nodiscard]] std::span<const std::byte> read_bytes(std::size_t size) {
        if (remaining() < size) {
            throw std::runtime_error("unexpected end of input");
        }
        auto result = bytes_.subspan(pos_, size);
        pos_ += size;
        return result;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t pos_ = 0;
};

class file_reader {
public:
    explicit file_reader(const std::string& path)
        : data_(read_all(path)), reader_(std::span<const std::byte>(data_.data(), data_.size())) {}

    [[nodiscard]] bool eof() const noexcept {
        return reader_.eof();
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return reader_.remaining();
    }

    [[nodiscard]] std::byte read_byte() {
        return reader_.read_byte();
    }

    [[nodiscard]] std::span<const std::byte> read_bytes(std::size_t size) {
        return reader_.read_bytes(size);
    }

private:
    static std::vector<std::byte> read_all(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("failed to open file for reading: " + path);
        }

        in.seekg(0, std::ios::end);
        const auto size = in.tellg();
        in.seekg(0, std::ios::beg);

        std::vector<std::byte> data(static_cast<std::size_t>(size));
        in.read(reinterpret_cast<char*>(data.data()), size);
        if (!in && size != 0) {
            throw std::runtime_error("failed to read file: " + path);
        }
        return data;
    }

    std::vector<std::byte> data_;
    memory_reader reader_;
};

struct archive_probe {
    template <class T>
    void field(std::uint32_t, T&) {}

    template <class T>
    void field(std::uint32_t, T&, omit_if_default_t) {}

    template <class T>
    archive_probe& operator()(field_id, T&) {
        return *this;
    }

    template <class T>
    archive_probe& operator()(field_id, T&, omit_if_default_t) {
        return *this;
    }
};

template <class T>
concept serializable = requires(T& value, archive_probe& probe) {
    value.serialize(probe);
};

template <serializable T>
std::size_t serialized_size(const T& value);

namespace detail {

template <class T>
inline constexpr bool always_false_v = false;

template <class Writer>
void write_byte(Writer& writer, std::byte byte) {
    writer.write(std::span<const std::byte>(&byte, 1));
}

template <class Writer>
void write_varint(Writer& writer, std::uint64_t value) {
    while (value >= 0x80) {
        write_byte(writer, static_cast<std::byte>((value & 0x7f) | 0x80));
        value >>= 7;
    }
    write_byte(writer, static_cast<std::byte>(value));
}

constexpr std::size_t varint_size(std::uint64_t value) {
    std::size_t size = 1;
    while (value >= 0x80) {
        ++size;
        value >>= 7;
    }
    return size;
}

template <class Reader>
std::uint64_t read_varint(Reader& reader) {
    std::uint64_t value = 0;
    for (int shift = 0; shift <= 63; shift += 7) {
        const auto byte = static_cast<std::uint8_t>(reader.read_byte());
        value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            return value;
        }
    }
    throw std::runtime_error("varint is too long");
}

template <class T>
constexpr std::make_unsigned_t<T> encode_zigzag(T value) {
    using U = std::make_unsigned_t<T>;
    using W = std::uint64_t;
    constexpr auto bits = std::numeric_limits<U>::digits;
    return static_cast<U>((static_cast<W>(static_cast<U>(value)) << 1) ^ static_cast<W>(value >> (bits - 1)));
}

template <class T>
constexpr T decode_zigzag(std::make_unsigned_t<T> value) {
    using U = std::make_unsigned_t<T>;
    using W = std::uint64_t;
    return static_cast<T>((static_cast<W>(value) >> 1) ^ (~static_cast<W>(value & U{1}) + W{1}));
}

static_assert(encode_zigzag<std::int8_t>(-1) == 1);
static_assert(decode_zigzag<std::int8_t>(1) == -1);
static_assert(decode_zigzag<std::int16_t>(encode_zigzag<std::int16_t>(-1234)) == -1234);
static_assert(decode_zigzag<std::int32_t>(encode_zigzag<std::int32_t>(-123456)) == -123456);
static_assert(decode_zigzag<std::int64_t>(encode_zigzag<std::int64_t>(-123456789)) == -123456789);

template <class Writer>
void write_fixed32(Writer& writer, std::uint32_t value) {
    std::byte bytes[4];
    for (int i = 0; i < 4; ++i) {
        bytes[i] = static_cast<std::byte>((value >> (i * 8)) & 0xff);
    }
    writer.write(bytes);
}

template <class Writer>
void write_fixed64(Writer& writer, std::uint64_t value) {
    std::byte bytes[8];
    for (int i = 0; i < 8; ++i) {
        bytes[i] = static_cast<std::byte>((value >> (i * 8)) & 0xff);
    }
    writer.write(bytes);
}

template <class Reader>
std::uint32_t read_fixed32(Reader& reader) {
    const auto bytes = reader.read_bytes(4);
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[i]) << (i * 8);
    }
    return value;
}

template <class Reader>
std::uint64_t read_fixed64(Reader& reader) {
    const auto bytes = reader.read_bytes(8);
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
    }
    return value;
}

template <class Reader>
void skip_payload(Reader& reader, wire_type type) {
    switch (type) {
    case wire_type::varint:
        (void)read_varint(reader);
        return;
    case wire_type::fixed32:
        (void)reader.read_bytes(4);
        return;
    case wire_type::fixed64:
        (void)reader.read_bytes(8);
        return;
    case wire_type::bytes: {
        const auto size = read_varint(reader);
        (void)reader.read_bytes(static_cast<std::size_t>(size));
        return;
    }
    }
    throw std::runtime_error("unknown wire type");
}

template <class T>
struct field_traits;

template <>
struct field_traits<bool> {
    static constexpr wire_type type = wire_type::varint;
};

template <>
struct field_traits<std::uint8_t> {
    static constexpr wire_type type = wire_type::varint;
};

template <>
struct field_traits<std::uint16_t> {
    static constexpr wire_type type = wire_type::varint;
};

template <>
struct field_traits<std::uint32_t> {
    static constexpr wire_type type = wire_type::varint;
};

template <>
struct field_traits<std::uint64_t> {
    static constexpr wire_type type = wire_type::varint;
};

template <>
struct field_traits<std::int8_t> {
    static constexpr wire_type type = wire_type::varint;
};

template <>
struct field_traits<std::int16_t> {
    static constexpr wire_type type = wire_type::varint;
};

template <>
struct field_traits<std::int32_t> {
    static constexpr wire_type type = wire_type::varint;
};

template <>
struct field_traits<std::int64_t> {
    static constexpr wire_type type = wire_type::varint;
};

template <>
struct field_traits<std::string> {
    static constexpr wire_type type = wire_type::bytes;
};

} // namespace detail

template <class Writer>
class basic_output_archive {
public:
    explicit basic_output_archive(Writer& writer)
        : writer_(writer) {}

    template <class T>
    void field(std::uint32_t id, const T& value) {
        static_assert(id_is_valid_message<T>(), "unsupported field type");
        constexpr auto type = field_wire_type<T>();
        write_key(id, type);
        write_value(value);
    }

    template <class T>
    void field(std::uint32_t id, const T& value, omit_if_default_t) {
        static_assert(id_is_valid_message<T>(), "unsupported field type");
        if (value == std::remove_cvref_t<T>{}) {
            return;
        }
        field(id, value);
    }

    template <class T>
    basic_output_archive& operator()(field_id id, const T& value) {
        field(id.value, value);
        return *this;
    }

    template <class T>
    basic_output_archive& operator()(field_id id, const T& value, omit_if_default_t omit) {
        field(id.value, value, omit);
        return *this;
    }

private:
    template <class T>
    static consteval bool id_is_valid_message() {
        return requires { detail::field_traits<std::remove_cvref_t<T>>::type; } || serializable<std::remove_cvref_t<T>>;
    }

    template <class T>
    static consteval wire_type field_wire_type() {
        if constexpr (requires { detail::field_traits<std::remove_cvref_t<T>>::type; }) {
            return detail::field_traits<std::remove_cvref_t<T>>::type;
        } else if constexpr (serializable<std::remove_cvref_t<T>>) {
            return wire_type::bytes;
        } else {
            static_assert(detail::always_false_v<T>, "unsupported field type");
        }
    }

    void write_key(std::uint32_t id, wire_type type) {
        if (id == 0) {
            throw std::runtime_error("field id 0 is reserved");
        }
        detail::write_varint(writer_, (static_cast<std::uint64_t>(id) << 3) | static_cast<std::uint8_t>(type));
    }

    void write_value(bool value) {
        detail::write_varint(writer_, value ? 1 : 0);
    }

    void write_value(std::uint8_t value) {
        detail::write_varint(writer_, value);
    }

    void write_value(std::uint16_t value) {
        detail::write_varint(writer_, value);
    }

    void write_value(std::uint32_t value) {
        detail::write_varint(writer_, value);
    }

    void write_value(std::uint64_t value) {
        detail::write_varint(writer_, value);
    }

    void write_value(std::int8_t value) {
        detail::write_varint(writer_, detail::encode_zigzag(value));
    }

    void write_value(std::int16_t value) {
        detail::write_varint(writer_, detail::encode_zigzag(value));
    }

    void write_value(std::int32_t value) {
        detail::write_varint(writer_, detail::encode_zigzag(value));
    }

    void write_value(std::int64_t value) {
        detail::write_varint(writer_, detail::encode_zigzag(value));
    }

    void write_value(const std::string& value) {
        detail::write_varint(writer_, value.size());
        writer_.write(std::as_bytes(std::span(value.data(), value.size())));
    }

    template <serializable T>
    void write_value(const T& value) {
        const auto size = serialized_size(value);
        detail::write_varint(writer_, size);
        basic_output_archive<Writer> nested_archive(writer_);
        const_cast<T&>(value).serialize(nested_archive);
    }

    Writer& writer_;
};

class size_archive {
public:
    template <class T>
    void field(std::uint32_t id, const T& value) {
        static_assert(id_is_valid_message<T>(), "unsupported field type");
        constexpr auto type = field_wire_type<T>();
        size_ += key_size(id, type);
        size_ += value_size(value);
    }

    template <class T>
    void field(std::uint32_t id, const T& value, omit_if_default_t) {
        static_assert(id_is_valid_message<T>(), "unsupported field type");
        if (value == std::remove_cvref_t<T>{}) {
            return;
        }
        field(id, value);
    }

    template <class T>
    size_archive& operator()(field_id id, const T& value) {
        field(id.value, value);
        return *this;
    }

    template <class T>
    size_archive& operator()(field_id id, const T& value, omit_if_default_t omit) {
        field(id.value, value, omit);
        return *this;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

private:
    template <class T>
    static consteval bool id_is_valid_message() {
        return requires { detail::field_traits<std::remove_cvref_t<T>>::type; } || serializable<std::remove_cvref_t<T>>;
    }

    template <class T>
    static consteval wire_type field_wire_type() {
        if constexpr (requires { detail::field_traits<std::remove_cvref_t<T>>::type; }) {
            return detail::field_traits<std::remove_cvref_t<T>>::type;
        } else if constexpr (serializable<std::remove_cvref_t<T>>) {
            return wire_type::bytes;
        } else {
            static_assert(detail::always_false_v<T>, "unsupported field type");
        }
    }

    static std::size_t key_size(std::uint32_t id, wire_type type) {
        if (id == 0) {
            throw std::runtime_error("field id 0 is reserved");
        }
        return detail::varint_size((static_cast<std::uint64_t>(id) << 3) | static_cast<std::uint8_t>(type));
    }

    static std::size_t value_size(bool value) {
        return detail::varint_size(value ? 1 : 0);
    }

    static std::size_t value_size(std::uint8_t value) {
        return detail::varint_size(value);
    }

    static std::size_t value_size(std::uint16_t value) {
        return detail::varint_size(value);
    }

    static std::size_t value_size(std::uint32_t value) {
        return detail::varint_size(value);
    }

    static std::size_t value_size(std::uint64_t value) {
        return detail::varint_size(value);
    }

    static std::size_t value_size(std::int8_t value) {
        return detail::varint_size(detail::encode_zigzag(value));
    }

    static std::size_t value_size(std::int16_t value) {
        return detail::varint_size(detail::encode_zigzag(value));
    }

    static std::size_t value_size(std::int32_t value) {
        return detail::varint_size(detail::encode_zigzag(value));
    }

    static std::size_t value_size(std::int64_t value) {
        return detail::varint_size(detail::encode_zigzag(value));
    }

    static std::size_t value_size(const std::string& value) {
        return detail::varint_size(value.size()) + value.size();
    }

    template <serializable T>
    static std::size_t value_size(const T& value) {
        const auto size = serialized_size(value);
        return detail::varint_size(size) + size;
    }

    std::size_t size_ = 0;
};

template <class Reader>
class basic_input_archive {
public:
    explicit basic_input_archive(Reader& reader)
        : reader_(reader) {}

    template <class T>
    void field(std::uint32_t id, T& value) {
        if (current_field_id_ == id) {
            read_value(current_wire_type_, value);
            consumed_ = true;
        }
    }

    template <class T>
    void field(std::uint32_t id, T& value, omit_if_default_t) {
        field(id, value);
    }

    template <class T>
    basic_input_archive& operator()(field_id id, T& value) {
        field(id.value, value);
        return *this;
    }

    template <class T>
    basic_input_archive& operator()(field_id id, T& value, omit_if_default_t omit) {
        field(id.value, value, omit);
        return *this;
    }

    template <serializable T>
    void read(T& value) {
        while (!reader_.eof()) {
            const auto key = detail::read_varint(reader_);
            current_field_id_ = static_cast<std::uint32_t>(key >> 3);
            current_wire_type_ = static_cast<wire_type>(key & 0x07);
            consumed_ = false;

            value.serialize(*this);

            if (!consumed_) {
                detail::skip_payload(reader_, current_wire_type_);
            }
        }
    }

private:
    void require_wire_type(wire_type actual, wire_type expected) {
        if (actual != expected) {
            throw std::runtime_error("wire type mismatch");
        }
    }

    void read_value(wire_type actual, bool& value) {
        require_wire_type(actual, wire_type::varint);
        value = detail::read_varint(reader_) != 0;
    }

    void read_value(wire_type actual, std::uint8_t& value) {
        require_wire_type(actual, wire_type::varint);
        value = static_cast<std::uint8_t>(detail::read_varint(reader_));
    }

    void read_value(wire_type actual, std::uint16_t& value) {
        require_wire_type(actual, wire_type::varint);
        value = static_cast<std::uint16_t>(detail::read_varint(reader_));
    }

    void read_value(wire_type actual, std::uint32_t& value) {
        require_wire_type(actual, wire_type::varint);
        value = static_cast<std::uint32_t>(detail::read_varint(reader_));
    }

    void read_value(wire_type actual, std::uint64_t& value) {
        require_wire_type(actual, wire_type::varint);
        value = detail::read_varint(reader_);
    }

    void read_value(wire_type actual, std::int8_t& value) {
        require_wire_type(actual, wire_type::varint);
        value = detail::decode_zigzag<std::int8_t>(static_cast<std::uint8_t>(detail::read_varint(reader_)));
    }

    void read_value(wire_type actual, std::int16_t& value) {
        require_wire_type(actual, wire_type::varint);
        value = detail::decode_zigzag<std::int16_t>(static_cast<std::uint16_t>(detail::read_varint(reader_)));
    }

    void read_value(wire_type actual, std::int32_t& value) {
        require_wire_type(actual, wire_type::varint);
        value = detail::decode_zigzag<std::int32_t>(static_cast<std::uint32_t>(detail::read_varint(reader_)));
    }

    void read_value(wire_type actual, std::int64_t& value) {
        require_wire_type(actual, wire_type::varint);
        value = detail::decode_zigzag<std::int64_t>(detail::read_varint(reader_));
    }

    void read_value(wire_type actual, std::string& value) {
        require_wire_type(actual, wire_type::bytes);
        const auto size = detail::read_varint(reader_);
        const auto bytes = reader_.read_bytes(static_cast<std::size_t>(size));
        value.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    template <serializable T>
    void read_value(wire_type actual, T& value) {
        require_wire_type(actual, wire_type::bytes);
        const auto size = detail::read_varint(reader_);
        const auto bytes = reader_.read_bytes(static_cast<std::size_t>(size));
        memory_reader nested_reader(bytes);
        basic_input_archive<memory_reader> nested_archive(nested_reader);
        nested_archive.read(value);
    }

    Reader& reader_;
    std::uint32_t current_field_id_ = 0;
    wire_type current_wire_type_ = wire_type::varint;
    bool consumed_ = false;
};

using output_archive = basic_output_archive<memory_writer>;
using input_archive = basic_input_archive<memory_reader>;

template <serializable T>
std::size_t serialized_size(const T& value) {
    size_archive archive;
    const_cast<T&>(value).serialize(archive);
    return archive.size();
}

template <serializable T>
std::vector<std::byte> serialize(const T& value) {
    memory_writer writer(serialized_size(value));
    basic_output_archive archive(writer);
    const_cast<T&>(value).serialize(archive);
    return std::move(writer).take_bytes();
}

template <serializable T>
std::size_t serialize(const T& value, void* data, std::size_t size) {
    const auto required_size = serialized_size(value);
    if (size < required_size) {
        throw std::runtime_error("output buffer is too small");
    }

    fixed_memory_writer writer(data, size);
    basic_output_archive archive(writer);
    const_cast<T&>(value).serialize(archive);
    return writer.size();
}

template <serializable T>
T deserialize(std::span<const std::byte> bytes) {
    T value{};
    memory_reader reader(bytes);
    basic_input_archive archive(reader);
    archive.read(value);
    return value;
}

template <serializable T>
void serialize_file(const std::string& path, const T& value) {
    file_writer writer(path);
    basic_output_archive archive(writer);
    const_cast<T&>(value).serialize(archive);
}

template <serializable T>
T deserialize_file(const std::string& path) {
    T value{};
    file_reader reader(path);
    basic_input_archive archive(reader);
    archive.read(value);
    return value;
}

} // namespace binflow
