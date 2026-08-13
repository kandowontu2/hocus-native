#include "dat_archive.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hocus {
namespace {

constexpr std::size_t registered_v1_1_dat_size = 6'101'525;
constexpr std::array<std::uint32_t,
                     DatArchive::registered_v1_1_asset_count>
    registered_v1_1_entry_sizes = {
#include "registered_v1_1_fat.inc"
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Cannot open " + path.string());
    }

    const auto end = stream.tellg();
    if (end < 0 || static_cast<unsigned long long>(end) >
                       std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Invalid file size for " + path.string());
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    stream.seekg(0);
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("Cannot read " + path.string());
    }
    return bytes;
}

} // namespace

DatArchive::DatArchive(const std::filesystem::path& dat_path)
    : DatArchive(read_file(dat_path)) {}

DatArchive::DatArchive(std::vector<std::uint8_t> dat_bytes)
    : dat_(std::move(dat_bytes)) {
    if (dat_.size() != registered_v1_1_dat_size) {
        throw std::runtime_error(
            "Unsupported HOCUS.DAT (registered version 1.1 is required)");
    }

    version_ = "registered-v1.1";
    entries_.reserve(registered_v1_1_asset_count);
    std::uint64_t offset = 0;
    for (std::size_t i = 0; i < registered_v1_1_entry_sizes.size(); ++i) {
        const auto size = registered_v1_1_entry_sizes[i];
        const auto end = static_cast<std::uint64_t>(offset) + size;
        if (end > dat_.size()) {
            throw std::runtime_error("HOCUS.DAT entry is out of bounds at index " +
                                     std::to_string(i));
        }
        entries_.push_back({static_cast<std::uint32_t>(offset), size});
        offset = end;
    }

    if (offset != dat_.size()) {
        throw std::runtime_error("HOCUS.DAT has unindexed trailing data");
    }
}

std::vector<std::uint8_t> DatArchive::read(std::size_t index) const {
    if (index >= entries_.size()) {
        throw std::out_of_range("Asset index is out of range");
    }
    const auto& entry = entries_[index];
    const auto begin = dat_.begin() + entry.offset;
    return {begin, begin + entry.size};
}

} // namespace hocus
