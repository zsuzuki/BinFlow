#include <binflow/binflow.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace binflow::literals;

enum class UserOption : std::uint32_t {
    CanPost = 0,
    CanUpload = 1,
    CanExport = 2,
    ReceivesDigest = 3,
    UsesBetaUi = 4,
    RequiresReview = 5,
    HasSso = 6,
    HasMfa = 7,
    CanInvite = 8,
    CanArchive = 9,
    CanShare = 10,
    CanDelete = 11,
    CanBill = 12,
    CanAudit = 13,
    IsInternal = 14,
};

struct Profile {
    std::string display_name;
    std::uint32_t reputation = 0;

    friend bool operator==(const Profile&, const Profile&) = default;

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
    static constexpr std::uint32_t default_quota = 100000;

    std::uint64_t id = 0;
    std::string name;
    bool active = false;
    std::uint32_t score = 100;
    std::uint32_t quota = default_quota;
    std::uint16_t level = 0;
    std::int32_t balance = 0;
    std::string note;
    binflow::flags32 options;
    std::vector<std::uint32_t> checkpoints;
    std::vector<std::int32_t> offsets;
    std::array<std::uint16_t, 4> buckets{};
    std::array<std::int32_t, 3> corrections{};
    Profile profile;
    std::vector<Profile> family_;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(1_f, id)
          (2_f, name)
          (3_f, active)
          (4_f, score)
          (5_f, profile)
          (6_f, level)
          (7_f, balance)
          (8_f, note, binflow::omit_if_default)
          (9_f, options)
          (10_f, quota, binflow::omit_if(default_quota))
          (11_f, checkpoints)
          (12_f, offsets)
          (13_f, buckets)
          (14_f, corrections)
          (15_f, family_);
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
        .options = binflow::flags32{0x7fff},
        .checkpoints = {1, 2, 300, 40000},
        .offsets = {-1, 0, 2, -300},
        .buckets = {3, 5, 8, 13},
        .corrections = {-2, 0, 2},
        .profile = {.display_name = "Alice A.", .reputation = 77},
        .family_ = {
            {.display_name = "Bob A.", .reputation = 41},
            {.display_name = "Carol A.", .reputation = 52},
        },
    };
    written.options.reset(UserOption::CanDelete);

    const auto expected_size = binflow::serialized_size(written);
    const auto bytes = binflow::serialize(written);
    std::vector<std::byte> external_buffer(expected_size);
    const auto external_size = binflow::serialize(written, external_buffer.data(), external_buffer.size());
    const bool external_matches = bytes == external_buffer;
    const auto delimited_bytes = binflow::serialize_delimited(written);
    std::vector<std::byte> delimited_stream = delimited_bytes;
    delimited_stream.push_back(std::byte{0xaa});
    delimited_stream.push_back(std::byte{0xbb});
    const auto [delimited_read, delimited_consumed] = binflow::deserialize_delimited<UserV2>(delimited_stream);
    const bool delimited_matches = delimited_read.checkpoints == written.checkpoints &&
                                   delimited_read.buckets == written.buckets &&
                                   delimited_read.family_ == written.family_ &&
                                   delimited_consumed == delimited_bytes.size();

    std::cout << "computed size: " << expected_size << " bytes\n";
    std::cout << "encoded size: " << bytes.size() << " bytes\n";
    std::cout << "external buffer size: " << external_size << " bytes\n";
    std::cout << "external buffer matches: " << std::boolalpha << external_matches << '\n';
    std::cout << "delimited size: " << delimited_bytes.size() << " bytes\n";
    std::cout << "delimited consumed: " << delimited_consumed << " bytes\n";
    std::cout << "delimited matches: " << delimited_matches << '\n';
    dump_hex(bytes);

    const auto read_v2 = binflow::deserialize<UserV2>(bytes);
    const bool vector_matches = read_v2.checkpoints == written.checkpoints && read_v2.offsets == written.offsets;
    const bool repeated_message_matches = read_v2.family_ == written.family_;
    const bool array_matches = read_v2.buckets == written.buckets && read_v2.corrections == written.corrections;
    std::cout << "v2: id=" << read_v2.id
              << ", name=" << read_v2.name
              << ", active=" << std::boolalpha << read_v2.active
              << ", score=" << read_v2.score
              << ", quota=" << read_v2.quota
              << ", level=" << read_v2.level
              << ", balance=" << read_v2.balance
              << ", note='" << read_v2.note << '\''
              << ", can_upload=" << read_v2.options.test(UserOption::CanUpload)
              << ", can_delete=" << read_v2.options.test(UserOption::CanDelete)
              << ", checkpoints=" << read_v2.checkpoints.size()
              << ", offsets=" << read_v2.offsets.size()
              << ", family=" << read_v2.family_.size()
              << ", vectors_match=" << vector_matches
              << ", repeated_messages_match=" << repeated_message_matches
              << ", arrays_match=" << array_matches
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
