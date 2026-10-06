#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace stl {
	constexpr std::uint32_t kMinSeed = 0x5F5E100;
	constexpr std::uint32_t kMaxSeed = 0x3B9AC9FF;

	class Seed final {
	public:
		static constexpr std::uint32_t kMinSeed = 0x5F5E100;
		static constexpr std::uint32_t kMaxSeed = 0x3B9AC9FF;

		static std::uint32_t NumberToSeed(double value) {
			if (value == 0)
				return 0;
			if (!std::isfinite(value))
				return kMinSeed;
			if (value < 0)
				value = -value;
			value = std::floor(value);
			if (value < static_cast<double>(kMinSeed))
				value += static_cast<double>(kMinSeed);
			if (value > static_cast<double>(kMaxSeed))
				value -= static_cast<double>(kMaxSeed);
			// Preserve the legacy mapping where representable; bound extreme inputs.
			if (value > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
				value = kMinSeed + std::fmod(value, static_cast<double>(kMaxSeed - kMinSeed + 1));
			return static_cast<std::uint32_t>(value);
		}

		template <typename T>
		static std::uint32_t NumberToSeed(T value) {
			static_assert(std::is_arithmetic<T>::value,
			              "NumberToSeed only supports arithmetic types");
			return NumberToSeed(static_cast<double>(value));
		}
		// Simple deterministic 64-bit hash (FNV-1a). Stable across runs/platforms.
		inline std::uint64_t HashString64(std::string_view sv) {
			constexpr std::uint64_t kOffset = 14695981039346656037ULL;
			constexpr std::uint64_t kPrime = 1099511628211ULL;
			std::uint64_t h = kOffset;
			for (const char c : sv) {
				h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
				h *= kPrime;
			}
			return h;
		}

		// Generate a deterministic arithmetic value of type T from input string.
		// Works for unsigned/signed integers and floating point types (float/double).
		template <typename T>
		inline T GenerateFromString(std::string_view sv) {
			static_assert(std::is_arithmetic_v<T>, "T must be arithmetic");
			const std::uint64_t h = HashString64(sv);

			if constexpr (std::is_same_v<T, bool>) {
				return (h & 1u) != 0;
			}
			else if constexpr (std::is_integral_v<T>) {
				// C++20 integral conversion gives the low bits without a full-width shift.
				return static_cast<T>(h);
			}
			else if constexpr (std::is_floating_point_v<T>) {
				if constexpr (std::is_same_v<T, double>) {
					// Use top 53 bits for double mantissa to produce uniform in [0,1).
					constexpr int mantissa_bits = 53;
					std::uint64_t mant = h >> (64 - mantissa_bits);
					return static_cast<double>(mant) /
					       static_cast<double>(std::uint64_t{1} << mantissa_bits);
				}
				else { // float
					constexpr int mantissa_bits = 24;
					std::uint64_t mant = h >> (64 - mantissa_bits);
					return static_cast<float>(mant) /
					       static_cast<float>(std::uint32_t{1} << mantissa_bits);
				}
			}
			else {
				// Should not reach here because of static_assert.
				return T{};
			}
		}

		template <typename T>
		static T GetRandomValue(const T& a, const T& b) {
			std::random_device device;
			std::mt19937 engine(device());
			return Sample(a, b, engine);
		}
		template <typename T>
		static T GetRandomValueSeed64(const T& a, const T& b, std::uint64_t seed) {
			std::mt19937_64 engine(seed);
			return Sample(a, b, engine);
		}
		template <typename T>
		static T GetRandomValueSeed32(const T& a, const T& b, std::uint32_t seed) {
			std::mt19937 engine(seed);
			return Sample(a, b, engine);
		}

	private:
		template <typename T, typename Engine>
		static T Sample(const T& a, const T& b, Engine& engine) {
			static_assert(std::is_arithmetic_v<T>, "T must be arithmetic");
			if (!(a <= b))
				throw std::invalid_argument("random range is reversed or NaN");
			if constexpr (std::is_floating_point_v<T>) {
				const auto low = static_cast<double>(a), high = static_cast<double>(b);
				if (!std::isfinite(low) || !std::isfinite(high) ||
				    static_cast<long double>(high) - low > std::numeric_limits<double>::max())
					throw std::invalid_argument("random range must be finite and representable");
				std::uniform_real_distribution<double> distribution(low, high);
				return static_cast<T>(distribution(engine));
			}
			else {
				// Preserve distributions for existing supported types; promote character/bool types.
				constexpr bool standard_integer = std::is_same_v<T, short> || std::is_same_v<T, unsigned short> ||
				                                  std::is_same_v<T, int> || std::is_same_v<T, unsigned int> || std::is_same_v<T, long> ||
				                                  std::is_same_v<T, unsigned long> || std::is_same_v<T, long long> || std::is_same_v<T, unsigned long long>;
				using Promoted = std::conditional_t<std::is_signed_v<T>, long long, unsigned long long>;
				using DistributionType = std::conditional_t<standard_integer, T, Promoted>;
				std::uniform_int_distribution<DistributionType> distribution(a, b);
				return static_cast<T>(distribution(engine));
			}
		}
	};
} // namespace stl
