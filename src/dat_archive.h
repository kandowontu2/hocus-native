#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hocus {

struct AssetEntry {
    std::uint32_t offset{};
    std::uint32_t size{};
};

class DatArchive {
public:
    static constexpr std::size_t registered_v1_1_asset_count = 652;

    explicit DatArchive(const std::filesystem::path& dat_path);
    explicit DatArchive(std::vector<std::uint8_t> dat_bytes);

    [[nodiscard]] const std::vector<AssetEntry>& entries() const noexcept {
        return entries_;
    }

    [[nodiscard]] std::vector<std::uint8_t> read(std::size_t index) const;
    [[nodiscard]] const std::string& version() const noexcept { return version_; }

private:
    std::vector<std::uint8_t> dat_;
    std::vector<AssetEntry> entries_;
    std::string version_;
};

} // namespace hocus
