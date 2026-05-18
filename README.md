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

struct User {
    std::uint64_t id = 0;
    std::string name;
    bool active = false;
    std::uint16_t level = 0;
    std::int32_t balance = 0;
    std::string note;
    binflow::flags32 options;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(1_f, id)
          (2_f, name)
          (3_f, active)
          (4_f, level)
          (5_f, balance)
          (6_f, note, binflow::omit_if_default)
          (7_f, options);
    }
};

User user;
user.options.set(UserOption::CanUpload);
```

Unsigned integers are varint encoded. Signed integers use zigzag varint, so
small negative values stay small. `omit_if_default` skips a field while writing
when the value equals its default-constructed value; while reading, the same
line keeps the default when the field is missing.

`flags32` and `flags64` pack groups of booleans into one varint field. Bit
positions are part of the persisted schema, so append new bits and do not reuse
old positions.

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

Build and run:

```sh
cmake -S . -B build
cmake --build build
./build/binflow_basic
```
