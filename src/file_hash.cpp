#include "file_hash.h"
#include <fstream>
#include <optional>
#include <openssl/sha.h>

static constexpr std::size_t hash_size = SHA_DIGEST_LENGTH;
static constexpr auto hash_function = &SHA1;
using hash_result_t = std::array<std::byte, hash_size>;

static hash_result_t hash_bytes(std::span<std::byte const> bytes)
{
	hash_result_t result;
	hash_function(
		reinterpret_cast<unsigned char const *>(bytes.data()),
		bytes.size(),
		reinterpret_cast<unsigned char *>(result.data())
	);

	return result;
}

static std::optional<hash_result_t> get_file_hash(fs::path const &filename)
{
	auto input_file = std::fstream(filename, std::ios::binary | std::ios::in);
	if (!input_file)
	{
		return std::nullopt;
	}

	input_file.seekg(0, std::ios::end);
	auto const size = input_file.tellg();
	input_file.seekg(0, std::ios::beg);

	auto file_data = std::vector<std::byte>(static_cast<std::size_t>(size));
	input_file.read(reinterpret_cast<char *>(file_data.data()), size);
	if (size != input_file.gcount())
	{
		return std::nullopt;
	}

	input_file.close();
	return hash_bytes(file_data);
}

static std::string to_string(hash_result_t const &hash)
{
	auto result = std::string();
	result.reserve(hash.size() * 2);
	for (auto const &byte : hash)
	{
		result += fmt::format("{:02x}", static_cast<std::uint8_t>(byte));
	}
	return result;
}

std::string hash_file(fs::path const &filename)
{
	if (auto const hash = get_file_hash(filename))
	{
		return to_string(*hash);
	}

	return "";
}
