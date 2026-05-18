#include <binflow/binflow.hpp>

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace binflow::literals;

struct Profile {
    std::string display_name;
    std::uint32_t reputation = 0;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(1_f, display_name)
          (2_f, reputation);
    }
};

struct UserV1 {
    std::uint64_t id = 0;
    std::string name;
    bool active = false;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(1_f, id)
          (2_f, name)
          (3_f, active);
    }
};

struct UserV2 {
    std::uint64_t id = 0;
    std::string name;
    bool active = false;
    std::uint32_t score = 100;
    std::uint16_t level = 0;
    std::int32_t balance = 0;
    std::string note;
    Profile profile;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(1_f, id)
          (2_f, name)
          (3_f, active)
          (4_f, score)
          (5_f, profile)
          (6_f, level)
          (7_f, balance)
          (8_f, note, binflow::omit_if_default);
    }
};

static void dump_hex(const std::vector<std::byte>& bytes) {
    for (std::byte byte : bytes) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(static_cast<unsigned char>(byte)) << ' ';
    }
    std::cout << std::dec << '\n';
}

int main() {
    UserV2 written{
        .id = 42,
        .name = "alice",
        .active = true,
        .score = 1200,
        .level = 12,
        .balance = -35,
        .profile = {.display_name = "Alice A.", .reputation = 77},
    };

    const auto bytes = binflow::serialize(written);

    std::cout << "encoded size: " << bytes.size() << " bytes\n";
    dump_hex(bytes);

    const auto read_v2 = binflow::deserialize<UserV2>(bytes);
    std::cout << "v2: id=" << read_v2.id
              << ", name=" << read_v2.name
              << ", active=" << std::boolalpha << read_v2.active
              << ", score=" << read_v2.score
              << ", level=" << read_v2.level
              << ", balance=" << read_v2.balance
              << ", note='" << read_v2.note << '\''
              << ", display_name=" << read_v2.profile.display_name
              << ", reputation=" << read_v2.profile.reputation << '\n';

    const auto read_v1 = binflow::deserialize<UserV1>(bytes);
    std::cout << "v1: id=" << read_v1.id
              << ", name=" << read_v1.name
              << ", active=" << std::boolalpha << read_v1.active << '\n';

    const std::string path = "build/sample.binflow";
    binflow::serialize_file(path, written);
    const auto from_file = binflow::deserialize_file<UserV2>(path);
    std::cout << "file: id=" << from_file.id
              << ", name=" << from_file.name
              << ", score=" << from_file.score << '\n';
}
