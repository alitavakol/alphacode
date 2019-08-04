#include <string>
#include <vector>
#include <boost/chrono.hpp>

#if defined(WIN32)
typedef unsigned __int64 uint64_t;
typedef unsigned int uint32_t;
#endif

class AlphaCode {
public:
	AlphaCode(size_t N, size_t C, size_t L, size_t R, size_t D);

	size_t N; // count of available symbols
	size_t C; // code length of each row [symbols]
	size_t L; // number of code lines
	size_t R; // redundancy symbol count
	size_t D; // number of redundancy symbols that are diffused into next code row

	static const char chars[]; // symbol building characters

	static const double timeout; // time interval in which a detected code row is considered as valid, and there is no need to spend time for detecting it again [s]

	std::vector< std::vector<unsigned int> > code;
	const static int map[][17]; // random index converter before accessing code

	std::vector< boost::chrono::time_point<boost::chrono::system_clock> > rowDetectTime;
	std::vector<bool> detected; // shows whether each code row is readable

	bool correct_not_expired(int row);
	uint64_t add_row(int row, unsigned int *code); // returns ticket number if all code rows are successfully detected, or -1 if fails

	std::string generate(uint64_t number);

	/* http://www.isthe.com/chongo/tech/comp/fnv/ */
	void fnvHash_D(unsigned int *x, int len, int seed, unsigned int *out);
	void fnvHash_R(unsigned int *x, int len, int seed, unsigned int *out);
	uint64_t TRUE_HASH_SIZE_R; // row hash value
	uint64_t TRUE_HASH_SIZE_D; // joint hash value
	const static uint32_t initial_seed;
	uint32_t seed;

	std::vector< std::vector<unsigned int> > joint_hash, self_hash;
};
