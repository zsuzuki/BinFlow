# BinFlow sample

C++20 header-only sample for a compact binary serialization format.

The wire format is a small TLV-style stream:

- key: `varint((field_id << 3) | wire_type)`
- value: varint, fixed-width value, or length-delimited bytes

Unknown fields are skipped, so adding new fields keeps old readers working.
Missing fields keep the C++ default value, so new readers can read old data.

```cpp
using namespace binflow::literals;

enum class UserOption : std::uint32_t {
    CanPost = 0,
    CanUpload = 1,
    UsesBetaUi = 2,
};

struct Profile {
    std::string display_name;
    std::uint32_t reputation = 0;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(1_f, display_name)
          (2_f, reputation);
    }
};

struct User {
    static constexpr std::uint32_t default_quota = 100000;

    std::uint64_t id = 0;
    std::string name;
    bool active = false;
    std::uint32_t quota = default_quota;
    std::uint16_t level = 0;
    std::int32_t balance = 0;
    std::string note;
    binflow::flags32 options;
    std::vector<std::uint32_t> checkpoints;
    std::vector<std::int32_t> offsets;
    std::array<std::uint16_t, 4> buckets{};
    std::vector<Profile> family;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(1_f, id)
          (2_f, name)
          (3_f, active)
          (4_f, level)
          (5_f, balance)
          (6_f, note, binflow::omit_if_default)
          (7_f, options)
          (8_f, quota, binflow::omit_if(default_quota))
          (9_f, checkpoints)
          (10_f, offsets)
          (11_f, buckets)
          (12_f, family);
    }
};

User user;
user.options.set(UserOption::CanUpload);
```

Unsigned integers are varint encoded. Signed integers use zigzag varint, so
small negative values stay small. `omit_if_default` skips a field while writing
when the value equals its default-constructed value; while reading, the same
line keeps the default when the field is missing.

Use `omit_if(value)` when the omitted value is not `T{}`. Keep that value in a
named constant so the member initializer and schema rule stay aligned.

`flags32` and `flags64` pack groups of booleans into one varint field. Bit
positions are part of the persisted schema, so append new bits and do not reuse
old positions.

Numeric vectors and arrays are encoded as packed length-delimited varint
payloads. Supported element types are unsigned and signed 8/16/32/64-bit
integers. Strings and `std::vector<bool>` are intentionally not part of packed
container support.

Vectors of serializable message types are encoded as repeated length-delimited
fields with the same field id, one nested message per element.

The exact encoded size can be computed without producing a byte buffer:

```cpp
const auto size = binflow::serialized_size(user);
auto bytes = binflow::serialize(user); // reserves the computed size first
```

Existing memory can also be used as the serialization target:

```cpp
std::vector<std::byte> buffer(binflow::serialized_size(user));
const auto written = binflow::serialize(user, buffer.data(), buffer.size());
```

Use delimited serialization when reading one object from a larger buffer or
stream:

```cpp
auto framed = binflow::serialize_delimited(user);
auto [read_user, consumed] = binflow::deserialize_delimited<User>(framed);
```

Delimited data is encoded as `varint(payload_size)` followed by the regular
payload. `consumed` includes both the size prefix and payload bytes.

Build and run:

```sh
cmake -S . -B build
cmake --build build
./build/binflow_basic
```
