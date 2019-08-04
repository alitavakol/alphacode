#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "fnv.hh"
#include "alphacode.h"
#include <math.h>
#include <iostream>

const uint32_t AlphaCode::initial_seed = EVENT_CODE;

const char AlphaCode::chars[] = { 0x2F /* / */, 0x5C /* \ */, 0x54 /* T */, 0x4C /* L */, 0x46 /* F */, 0x48 /* H */, 0x58 /* X */, 0x2B /* + */};

const double AlphaCode::timeout = 1.0; // [s]

#if !defined (PATENT)
const int AlphaCode::map[][17] = { { 3, 7, 14, 15, 11, 6, 4, 2, 12, 1, 16, 5, 13, 0, 8, 9, 10 }, { 13, 14, 0, 16, 8, 9, 12, 2, 4, 10, 1, 3, 7,
	5, 11, 6, 15 }, { 8, 14, 2, 9, 6, 11, 3, 7, 13, 16, 0, 4, 10, 12, 15, 1, 5 }, { 15, 8, 0, 7, 16, 11, 13, 1, 10, 9, 6, 14, 4, 3, 5,
	2, 12 } };
#else
const int AlphaCode::map[][17] = { { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 }, 
		{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 } };
#endif

AlphaCode::AlphaCode(size_t N_, size_t C_, size_t L_, size_t R_, size_t D_)
		: N(N_), C(C_), L(L_), R(R_), D(D_)
{
	if (!((2 <= N && N <= sizeof(chars)/sizeof(chars[0])) && (1 <= L && L <= 4) && (R >= D && D >= 0) && (R < C && C <= sizeof(map[0])/sizeof(map[0][0])))) {
		std::cerr << "bad parameters\n";
		exit(-1);
	}

	code.resize(L);
	for (size_t i = 0; i < L; i++) {
		code[i].resize(C);
	}

	joint_hash.resize(L);
	self_hash.resize(L);
	for (size_t i = 0; i < L; i++) {
		joint_hash[i].resize(D);
		self_hash[i].resize(R-D);
	}

	rowDetectTime.resize(L);
	detected.resize(L);

	for (size_t row = 0; row < L; row++)
		detected[row] = false;
	seed = initial_seed;

	TRUE_HASH_SIZE_D = (uint64_t) pow((float) N, (float) D);
	TRUE_HASH_SIZE_R = (uint64_t) pow((float) N, (float) (R-D));
}

bool AlphaCode::correct_not_expired(int row)
{
	if (!detected[row])
		return false;

	double delay = (double) (boost::chrono::system_clock::now() - rowDetectTime[row]).count()
	        * (double) boost::chrono::system_clock::period::num / boost::chrono::system_clock::period::den;
	detected[row] = delay < timeout;

	return detected[row];
}

uint64_t AlphaCode::add_row(int row, unsigned int *code_)
{
	rowDetectTime[row] = boost::chrono::system_clock::now();

	for (size_t j = 0, k = 0; j < C; k++) {
		if(map[row][k] < (int) C) {
			code[row][map[row][k]] = code_[j];
			j++;
		}
	}

	fnvHash_R(&code[row][0], C - R + D, seed, &self_hash[row][0]);
	detected[row] = true;
	for (size_t j = 0; j < R - D; j++) {
		if (code[row][C - R + D + j] != self_hash[row][j]) {
			detected[row] = false;
			break;
		}
	}

	for (size_t r = 0; r < L; r++) {
		if (!correct_not_expired(r))
			return 0;
	}

	for (int r = L - 1; r >= 0; r--) {
		fnvHash_D(&code[r][0], C - R, seed, &joint_hash[r][0]);
//		detected[r] = false;
	}

	/* verify joint checksum */
	for (size_t r = 0; r < L; r++) {
		for (size_t j = 0; j < D; j++) {
			if (code[(r + 1) % L][C - R + j] != joint_hash[r][j]) {
				return 0;
			}
		}
	}

	uint64_t codeNumber = 0;

	for (size_t r = 0; r < L; r++) {
		for (size_t j = 0; j < C - R; j++)
			codeNumber = codeNumber * N + this->code[r][j];
	}

	return codeNumber;
}

std::string AlphaCode::generate(uint64_t number)
{
	for (int r = L - 1; r >= 0; r--) {
		for (int j = C - R - 1; j >= 0; j--) {
			code[r][j] = number % N;
			number /= N;
		}
		fnvHash_D(&code[r][0], C - R, seed, &joint_hash[r][0]);
		for (size_t j = 0; j < D; j++)
			code[(r + 1) % L][C - R + j] = joint_hash[r][j];
	}

	for (int r = L - 1; r >= 0; r--) {
		fnvHash_R(&code[r][0], C - R + D, seed, &self_hash[r][0]);
		for (size_t j = 0; j < R - D; j++)
			code[r][C - R + D + j] = self_hash[r][j];
	}

	std::stringstream ss;
	for (size_t i = 0; i < spaceBeforeRow; i++)
		ss << ' ';
	for (size_t i = 0; i < codeFinderSlashCount; i++)
		ss << '/';
	ss << '\n';
	for (size_t r = 0; r < L; r++) {
		if (r)
			ss << '\n';
		for (size_t i = 0; i < spaceBeforeRow; i++)
			ss << ' ';
		for(size_t i = 0; i < rowFinderSlashCount; i++)
			ss << '/';
		for (size_t j = 0, k = 0; j < C; k++) {
			if(map[r][k] < (int) C) {
				ss << chars[code[r][map[r][k]]];
				code[r][map[r][k]] = -1;
				j++;
			}
		}
	}

	return ss.str();
}

void AlphaCode::fnvHash_D(unsigned int *x, int len, int seed, unsigned int *result)
{
	static char hash_in[30];
	int i, j, k;

	uint64_t value = 0;
	for (i = 0; i < len; i++)
		value += (uint64_t) (x[i] * pow((float) N, (float) i));

	for (i = (int) (log((float) value) / log((float) 8)), j = 0; i >= 0; i--, j++) {
		k = (int) pow((float) 8, (float) i);
		hash_in[j] = (char) (value / k);
		value -= k * hash_in[j];
	}
	hash_in[j] = 0;

	static hash::fnv<32> fnv;
	fnv.offset(seed);
	uint64_t hash = fnv(hash_in);

	/* http://www.isthe.com/chongo/tech/comp/fnv/#other-folding */
	hash %= TRUE_HASH_SIZE_D;

	for (i = D - 1; i >= 0; i--) {
		k = (int) pow((float) N, (float) i);
		result[i] = (unsigned int) (hash / k);
		hash -= k * result[i];
	}
}

void AlphaCode::fnvHash_R(unsigned int *x, int len, int seed, unsigned int *result)
{
	static char hash_in[30];
	int i, j, k;

	uint64_t value = 0;
	for (i = 0; i < len; i++)
		value += (uint64_t) (x[i] * pow((float) N, (float) i));

	for (i = (int) (log((float) value) / log((float) 8)), j = 0; i >= 0; i--, j++) {
		k = (int) pow((float) 8, (float) i);
		hash_in[j] = (char) (value / k);
		value -= k * hash_in[j];
	}
	hash_in[j] = 0;

	static hash::fnv<32> fnv;
	fnv.offset(seed);
	uint64_t hash = fnv(hash_in);

	/* http://www.isthe.com/chongo/tech/comp/fnv/#other-folding */
	hash %= TRUE_HASH_SIZE_R;

	for (i = R - D - 1; i >= 0; i--) {
		k = (int) pow((float) N, (float) i);
		result[i] = (unsigned int) (hash / k);
		hash -= k * result[i];
	}
}
