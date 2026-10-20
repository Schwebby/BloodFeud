#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace ledger
{
	// Small deterministic PRNG (xoshiro128**). State is saved with the game so
	// a reloaded save rolls the same dice.
	class Rng
	{
	public:
		explicit Rng(std::uint64_t seed = 0x9E3779B97F4A7C15ull) { Seed(seed); }

		void Seed(std::uint64_t seed)
		{
			for (auto& s : _s) {
				seed += 0x9E3779B97F4A7C15ull;
				std::uint64_t z = seed;
				z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
				z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
				s = static_cast<std::uint32_t>(z ^ (z >> 31));
			}
			if ((_s[0] | _s[1] | _s[2] | _s[3]) == 0) {
				_s[0] = 1;
			}
		}

		std::uint32_t Next()
		{
			const std::uint32_t result = Rotl(_s[1] * 5, 7) * 9;
			const std::uint32_t t = _s[1] << 9;
			_s[2] ^= _s[0];
			_s[3] ^= _s[1];
			_s[1] ^= _s[2];
			_s[0] ^= _s[3];
			_s[2] ^= t;
			_s[3] = Rotl(_s[3], 11);
			return result;
		}

		// [0, n)
		std::uint32_t Below(std::uint32_t n) { return n ? static_cast<std::uint32_t>((static_cast<std::uint64_t>(Next()) * n) >> 32) : 0; }
		// [lo, hi]
		int Range(int lo, int hi) { return hi <= lo ? lo : lo + static_cast<int>(Below(static_cast<std::uint32_t>(hi - lo + 1))); }
		float Unit() { return static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f); }
		bool Chance(float p) { return Unit() < p; }

		template <class T>
		const T& Pick(std::span<const T> items)
		{
			return items[Below(static_cast<std::uint32_t>(items.size()))];
		}
		template <class T>
		const T& Pick(const std::vector<T>& items) { return items[Below(static_cast<std::uint32_t>(items.size()))]; }

		std::uint32_t* State() { return _s; }
		const std::uint32_t* State() const { return _s; }

	private:
		static std::uint32_t Rotl(std::uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }
		std::uint32_t _s[4]{};
	};
}
