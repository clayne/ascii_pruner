// pruning of blanks from an ascii stream -- timing of candidate routines
#if defined(__ARM_FEATURE_SVE)
	#include <arm_sve.h>
#endif
#if __aarch64__
	#include <arm_neon.h>
#elif __SSSE3__
	#include <tmmintrin.h>
#elif __SSE2__
	#include <emmintrin.h>
#endif
#if __POPCNT__
	#include <popcntintrin.h>
#endif
#include <stdio.h>
#include <stdint.h>

uint8_t input[64] __attribute__ ((aligned(64))) =
	"012345 6789  abc"
	"def 123456789abc";
uint8_t output[64] __attribute__ ((aligned(64)));

// print utility
#if __aarch64__
void print_uint8x16(
	uint8x16_t const x,
	bool const addNewLine,
	FILE* const f = stderr) {

	fprintf(f, "{ ");
#define LANE(lane) \
	uint8_t const s##lane = vgetq_lane_u8(x, lane); \
	fprintf(f, "%.3hhu, ", s##lane);

	LANE( 0)
	LANE( 1)
	LANE( 2)
	LANE( 3)
	LANE( 4)
	LANE( 5)
	LANE( 6)
	LANE( 7)

	LANE( 8)
	LANE( 9)
	LANE(10)
	LANE(11)
	LANE(12)
	LANE(13)
	LANE(14)

#undef LANE
	uint8_t const last = vgetq_lane_u8(x, 15);

	if (addNewLine)
		fprintf(f, "%.3hhu }\n", last);
	else
		fprintf(f, "%.3hhu }", last);
}

void print_uint8x8(
	uint8x8_t const x,
	bool const addNewLine,
	FILE* const f = stderr) {

	fprintf(f, "{ ");
#define LANE(lane) \
	uint8_t const s##lane = vget_lane_u8(x, lane); \
	fprintf(f, "%.3hhu, ", s##lane);

	LANE( 0)
	LANE( 1)
	LANE( 2)
	LANE( 3)
	LANE( 4)
	LANE( 5)
	LANE( 6)

#undef LANE
	uint8_t const last = vget_lane_u8(x, 7);

	if (addNewLine)
		fprintf(f, "%.3hhu }\n", last);
	else
		fprintf(f, "%.3hhu }", last);
}

#elif __SSE2__
void print_uint8x16(
	__m128i const x,
	bool const addNewLine,
	FILE* const f = stderr) {

	fprintf(f, "{ ");

	uint64_t head = _mm_cvtsi128_si64(x);
	for (size_t j = 0; j < sizeof(head) / sizeof(uint8_t); ++j) {
		fprintf(f, "%.3hhu, ", uint8_t(head));
		head >>= sizeof(uint8_t) * 8;
	}

	uint64_t tail = _mm_cvtsi128_si64(_mm_shuffle_epi32(x, 0xee));
	for (size_t j = 0; j < sizeof(tail) / sizeof(uint8_t) - 1; ++j) {
		fprintf(f, "%.3hhu, ", uint8_t(tail));
		tail >>= sizeof(uint8_t) * 8;
	}

	if (addNewLine)
		fprintf(f, "%.3hhu }\n", uint8_t(tail));
	else
		fprintf(f, "%.3hhu }", uint8_t(tail));
}

#endif
// fully-scalar version; good performance on both amd64 and arm64 above-entry-level parts;
// particularly on cortex-a72 this does an IPC of 2.94 which is excellent! ryzen also
// does an IPC above 4, which is remarkable
inline size_t testee00() {
	size_t i = 0, pos = 0;
	while (i < 16) {
		const char c = input[i++];
		output[pos] = c;
		pos += (c > 32 ? 1 : 0);
	}
	return pos;
}

#if __aarch64__
// naive pruner, 16-batch; filter single blank from N input chars, followed by K optional trailing blanks, N + K = batch size
// example: "1234 678  " -> "1234678" (N + K = 10)
inline size_t testee01() {
	uint8x16_t const vinput = vld1q_u8(input);
	uint8x16_t prfsum = vcleq_u8(vinput, vdupq_n_u8(' '));

	// pick one:
	// before computing the prefix sum: count_of_blanks := vaddvq_u8(prfsum)
	// after computing the prefix sum: count_of_blanks := prfsum[last_lane]
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 1));
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 2));
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 4));
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 8));

	int8_t const bnum = vgetq_lane_u8(prfsum, 15);

	uint8x16_t const index = vsubq_u8((uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, prfsum);
	uint8x16_t const res = vqtbl1q_u8(vinput, index);

	vst1q_u8(output, res);
	return sizeof(uint8x16_t) + bnum;
}

// naive pruner, 32-batch; filter single blank from N input chars, followed by K optional trailing blanks, N + K = half batch size
// example: "1234 678  " -> "1234678" (N + K = 10)
inline size_t testee02() {
	uint8x16_t const vinput0 = vld1q_u8(input);
	uint8x16_t const vinput1 = vld1q_u8(input + sizeof(uint8x16_t));
	uint8x16_t prfsum0 = vcleq_u8(vinput0, vdupq_n_u8(' '));
	uint8x16_t prfsum1 = vcleq_u8(vinput1, vdupq_n_u8(' '));

	// pick one:
	// before computing the prefix sum: count_of_blanks := vaddvq_u8(prfsum)
	// after computing the prefix sum: count_of_blanks := prfsum[last_lane]
	prfsum0 = vaddq_u8(prfsum0, vextq_u8(vdupq_n_u8(0), prfsum0, 16 - 1));
	prfsum1 = vaddq_u8(prfsum1, vextq_u8(vdupq_n_u8(0), prfsum1, 16 - 1));
	prfsum0 = vaddq_u8(prfsum0, vextq_u8(vdupq_n_u8(0), prfsum0, 16 - 2));
	prfsum1 = vaddq_u8(prfsum1, vextq_u8(vdupq_n_u8(0), prfsum1, 16 - 2));
	prfsum0 = vaddq_u8(prfsum0, vextq_u8(vdupq_n_u8(0), prfsum0, 16 - 4));
	prfsum1 = vaddq_u8(prfsum1, vextq_u8(vdupq_n_u8(0), prfsum1, 16 - 4));
	prfsum0 = vaddq_u8(prfsum0, vextq_u8(vdupq_n_u8(0), prfsum0, 16 - 8));
	prfsum1 = vaddq_u8(prfsum1, vextq_u8(vdupq_n_u8(0), prfsum1, 16 - 8));

	int8_t const bnum0 = vgetq_lane_u8(prfsum0, 15);
	int8_t const bnum1 = vgetq_lane_u8(prfsum1, 15);

	uint8x16_t const index0 = vsubq_u8((uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, prfsum0);
	uint8x16_t const index1 = vsubq_u8((uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, prfsum1);
	uint8x16_t const res0 = vqtbl1q_u8(vinput0, index0);
	uint8x16_t const res1 = vqtbl1q_u8(vinput1, index1);

	vst1q_u8(output, res0);
	vst1q_u8(output + sizeof(uint8x16_t) + bnum0, res1);
	return sizeof(uint8x16_t) * 2 + bnum0 + bnum1;
}

#elif __SSSE3__
// naive pruner, 16-batch; filter single blank from N input chars, followed by K optional trailing blanks, N + K = batch size
// example: "1234 678  " -> "1234678" (N + K = 10)
inline size_t testee01() {
	__m128i const vinput = _mm_load_si128(reinterpret_cast< const __m128i* >(input));
	__m128i prfsum = _mm_cmplt_epi8(vinput, _mm_set1_epi8(' ' + 1));

	// pick one:
	// before computing the prefix sum: count_of_blanks := _mm_popcnt_u32(_mm_movemask_epi8(prfsum))
	// after computing the prefix sum: count_of_blanks := prfsum[last_lane]
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 1));
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 2));
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 4));
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 8));

	int8_t const bnum = uint16_t(_mm_extract_epi16(prfsum, 7)) >> 8;

	__m128i const index = _mm_sub_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), prfsum);
	__m128i const res = _mm_shuffle_epi8(vinput, index);

	_mm_storeu_si128(reinterpret_cast< __m128i* >(output), res);
	return sizeof(__m128i) + bnum;
}

// naive pruner, 32-batch; filter single blank from N input chars, followed by K optional trailing blanks, N + K = half batch size
// example: "1234 678  " -> "1234678" (N + K = 10)
inline size_t testee02() {
	__m128i const vinput0 = _mm_load_si128(reinterpret_cast< const __m128i* >(input));
	__m128i const vinput1 = _mm_load_si128(reinterpret_cast< const __m128i* >(input) + 1);
	__m128i prfsum0 = _mm_cmplt_epi8(vinput0, _mm_set1_epi8(' ' + 1));
	__m128i prfsum1 = _mm_cmplt_epi8(vinput1, _mm_set1_epi8(' ' + 1));

	// pick one:
	// before computing the prefix sum: count_of_blanks := _mm_popcnt_u32(_mm_movemask_epi8(prfsum))
	// after computing the prefix sum: count_of_blanks := prfsum[last_lane]
	prfsum0 = _mm_add_epi8(prfsum0, _mm_slli_si128(prfsum0, 1));
	prfsum1 = _mm_add_epi8(prfsum1, _mm_slli_si128(prfsum1, 1));
	prfsum0 = _mm_add_epi8(prfsum0, _mm_slli_si128(prfsum0, 2));
	prfsum1 = _mm_add_epi8(prfsum1, _mm_slli_si128(prfsum1, 2));
	prfsum0 = _mm_add_epi8(prfsum0, _mm_slli_si128(prfsum0, 4));
	prfsum1 = _mm_add_epi8(prfsum1, _mm_slli_si128(prfsum1, 4));
	prfsum0 = _mm_add_epi8(prfsum0, _mm_slli_si128(prfsum0, 8));
	prfsum1 = _mm_add_epi8(prfsum1, _mm_slli_si128(prfsum1, 8));

	int8_t const bnum0 = uint16_t(_mm_extract_epi16(prfsum0, 7)) >> 8;
	int8_t const bnum1 = uint16_t(_mm_extract_epi16(prfsum1, 7)) >> 8;

	__m128i const index0 = _mm_sub_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), prfsum0);
	__m128i const index1 = _mm_sub_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), prfsum1);
	__m128i const res0 = _mm_shuffle_epi8(vinput0, index0);
	__m128i const res1 = _mm_shuffle_epi8(vinput1, index1);

	_mm_storeu_si128(reinterpret_cast< __m128i* >(output), res0);
	_mm_storeu_si128(reinterpret_cast< __m128i* >(output + sizeof(__m128i) + bnum0), res1);
	return sizeof(__m128i) * 2 + bnum0 + bnum1;
}

#if __POPCNT__
// pruner semi, 16-batch; replace blanks with the next non-blank, cutting off trailing blanks from the batch
// example: "1234 678  " -> "12346678"
inline size_t testee03() {
	__m128i const vin = _mm_load_si128(reinterpret_cast< const __m128i* >(input));

	// discover non-blanks
	__m128i const pos = _mm_cmpgt_epi8(vin, _mm_set1_epi8(' '));

	// mark blanks as ones
	__m128i const spc = _mm_andnot_si128(pos, _mm_set1_epi8(1));

	// prefix-sum the blanks, right to left
	__m128i prfsum = spc;
	prfsum = _mm_add_epi8(prfsum, _mm_srli_si128(prfsum, 1));
	prfsum = _mm_add_epi8(prfsum, _mm_srli_si128(prfsum, 2));
	prfsum = _mm_add_epi8(prfsum, _mm_srli_si128(prfsum, 4));
	prfsum = _mm_add_epi8(prfsum, _mm_srli_si128(prfsum, 8));

	// isolate sequences of blanks and count their individual lengths, right to left, using a prefix max and the above prefix sum
	__m128i prfmax = _mm_and_si128(pos, prfsum);
	prfmax = _mm_max_epu8(prfmax, _mm_srli_si128(prfmax, 1));
	prfmax = _mm_max_epu8(prfmax, _mm_srli_si128(prfmax, 2));
	prfmax = _mm_max_epu8(prfmax, _mm_srli_si128(prfmax, 4));
	prfmax = _mm_max_epu8(prfmax, _mm_srli_si128(prfmax, 8));
	prfmax = _mm_sub_epi8(prfsum, prfmax);

	// add blank counts to a sequential index to get non-blanks index
	__m128i const index = _mm_add_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), prfmax);

	// cap trailing out-of-bounds in the index
	__m128i const indey = _mm_or_si128(index, _mm_cmpgt_epi8(index, _mm_set1_epi8(15)));

	// use the index to fetch all non-blanks from the dictionary
	__m128i const res = _mm_shuffle_epi8(vin, indey);

	_mm_storeu_si128(reinterpret_cast< __m128i* >(output), res);
	return sizeof(__m128i) - _mm_popcnt_u32(_mm_movemask_epi8(indey));
}

#endif
#endif
// From here on start the proper pruners. They all implement the following idea:
//
//  1. Get the unperturbed index of all elements of a vector, e.g. { 0, 1, 2, 3, 4, 5, 6, 7 } for an 8-element vector
//  2. For all blank lanes in the input ascii vector, raise the corresponding lanes in the index vector to MAX_INT, e.g. { 0, 1, 0xff, 3, 0xff, 0xff, 6, 7 }
//  3. Sort the resulting index vector in ascending order, e.g. { 0, 1, 3, 6, 7, 0xff, 0xff, 0xff }
//
// This is the desired index by which to sample the original input vector. That's all.

#if __aarch64__
// pruner proper, 16-batch; q-form (128-bit regs) half-utilized
inline size_t testee04() {
	uint8x16_t const vin = vld1q_u8(input);
	uint8x16_t const bmask = vcleq_u8(vin, vdupq_n_u8(' '));

	// OR the mask of all blanks with the original index of the vector
	uint8x16_t const risen = vorrq_u8(bmask, (uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });

	// now just sort that 'risen' to get the desired index of all non-blanks in the front, and all blanks in the back

	// 16-element sorting network: http://pages.ripco.net/~jgamble/nw.html -- 'Best version'
	// stage 0
	uint8x16_t const st0a = vqtbl1q_u8(risen, (uint8x16_t) { 0, 2, 4, 6, 8, 10, 12, 14, });
	uint8x16_t const st0b = vqtbl1q_u8(risen, (uint8x16_t) { 1, 3, 5, 7, 9, 11, 13, 15, });
	uint8x16_t const st0min = vminq_u8(st0a, st0b); //  0,  2,  4,  6,  8, 10, 12, 14
	uint8x16_t const st0max = vmaxq_u8(st0a, st0b); //  1,  3,  5,  7,  9, 11, 13, 15

	// stage 1
	uint8x16x2_t const st0 = { { st0min, st0max } };
	uint8x16_t const st1a = vqtbl2q_u8(st0, (uint8x16_t) { 0, 2, 4, 6, 16, 18, 20, 22, });
	uint8x16_t const st1b = vqtbl2q_u8(st0, (uint8x16_t) { 1, 3, 5, 7, 17, 19, 21, 23, });
	uint8x16_t const st1min = vminq_u8(st1a, st1b); //  0,  4,  8, 12,  1,  5,  9, 13
	uint8x16_t const st1max = vmaxq_u8(st1a, st1b); //  2,  6, 10, 14,  3,  7, 11, 15

	// stage 2
	uint8x16x2_t const st1 = { { st1min, st1max } };
	uint8x16_t const st2a = vqtbl2q_u8(st1, (uint8x16_t) { 0, 2, 4, 6, 16, 18, 20, 22, });
	uint8x16_t const st2b = vqtbl2q_u8(st1, (uint8x16_t) { 1, 3, 5, 7, 17, 19, 21, 23, });
	uint8x16_t const st2min = vminq_u8(st2a, st2b); //  0,  8,  1,  9,  2, 10,  3, 11
	uint8x16_t const st2max = vmaxq_u8(st2a, st2b); //  4, 12,  5, 13,  6, 14,  7, 15

	// stage 3
	uint8x16x2_t const st2 = { { st2min, st2max } };
	uint8x16_t const st3a = vqtbl2q_u8(st2, (uint8x16_t) { 0, 2, 4, 6, 16, 18, 20, 22, });
	uint8x16_t const st3b = vqtbl2q_u8(st2, (uint8x16_t) { 1, 3, 5, 7, 17, 19, 21, 23, });
	uint8x16_t const st3min = vminq_u8(st3a, st3b); // 0, 1,  2,  3,  4,  5,  6,  7
	uint8x16_t const st3max = vmaxq_u8(st3a, st3b); // 8, 9, 10, 11, 12, 13, 14, 15

	// from here on some indices are already done -- freeze them, by keeping them in deterministic positions

	// stage 4; indices done so far: 0, 15
	uint8x16x2_t const st3 = { { st3min, st3max } };
	uint8x16_t const st4a = vqtbl2q_u8(st3, (uint8x16_t) {  0,  5,  6,  3, 21,  7, 1,  4, });
	uint8x16_t const st4b = vqtbl2q_u8(st3, (uint8x16_t) { 23, 18, 17, 20, 22, 19, 2, 16, });
	uint8x16_t const st4min = vminq_u8(st4a, st4b); // [ 0],  5,  6,  3, 13,  7,  1,  4
	uint8x16_t const st4max = vmaxq_u8(st4a, st4b); // [15], 10,  9, 12, 14, 11,  2,  8

	// stage 5; done so far: 0, 15; temp frozen: 3, 12
	uint8x16x2_t const st4 = { { st4min, st4max } };
	uint8x16_t const st5a = vqtbl2q_u8(st4, (uint8x16_t) {  0,  3, 6, 5, 22, 21, 1, 18, });
	uint8x16_t const st5b = vqtbl2q_u8(st4, (uint8x16_t) { 16, 19, 7, 4, 23, 20, 2, 17, });
	uint8x16_t const st5min = vminq_u8(st5a, st5b); // [ 0], [ 3], 1,  7, 2, 11, 5,  9
	uint8x16_t const st5max = vmaxq_u8(st5a, st5b); // [15], [12], 4, 13, 8, 14, 6, 10

	// stage 6; done so far: 0, 1, 14, 15; temp frozen: 5, 6, 9, 10
	uint8x16x2_t const st5 = { { st5min, st5max } };
	uint8x16_t const st6a = vqtbl2q_u8(st5, (uint8x16_t) {  0,  2,  4,  5,  1,  3,  6,  7, });
	uint8x16_t const st6b = vqtbl2q_u8(st5, (uint8x16_t) { 16, 21, 18, 19, 20, 17, 22, 23, });
	uint8x16_t const st6min = vminq_u8(st6a, st6b); // [ 0], [ 1], 2, 11, 3,  7, [5], [ 9]
	uint8x16_t const st6max = vmaxq_u8(st6a, st6b); // [15], [14], 4, 13, 8, 12, [6], [10]

	// stage 7; done so far: 0, 1, 2, 13, 14, 15; temp frozen: 4, 11
	uint8x16x2_t const st6 = { { st6min, st6max } };
	uint8x16_t const st7a = vqtbl2q_u8(st6, (uint8x16_t) {  0,  1,  2,  3, 22, 23, 4, 5, });
	uint8x16_t const st7b = vqtbl2q_u8(st6, (uint8x16_t) { 16, 17, 18, 19, 20, 21, 6, 7, });
	uint8x16_t const st7min = vminq_u8(st7a, st7b); // [ 0], [ 1], [2], [11], 6, 10, 3, 7
	uint8x16_t const st7max = vmaxq_u8(st7a, st7b); // [15], [14], [4], [13], 8, 12, 5, 9

	// stage 8; done so far: 0, 1, 2, 13, 14, 15
	uint8x16x2_t const st7 = { { st7min, st7max } };
	uint8x16_t const st8a = vqtbl2q_u8(st7, (uint8x16_t) {  0,  1,  2,  6, 22,  7, 23,  3, });
	uint8x16_t const st8b = vqtbl2q_u8(st7, (uint8x16_t) { 16, 17, 19, 18,  4, 20,  5, 21, });
	uint8x16_t const st8min = vminq_u8(st8a, st8b); // [ 0], [ 1], [ 2], 3, 5, 7,  9, 11
	uint8x16_t const st8max = vmaxq_u8(st8a, st8b); // [15], [14], [13], 4, 6, 8, 10, 12

	// stage 9; done so far: 0, 1, 2, 3, 4, 5, 10, 11, 12, 13, 14, 15
	uint8x16x2_t const st8 = { { st8min, st8max } };
	uint8x16_t const st9a = vqtbl2q_u8(st8, (uint8x16_t) {  0,  1,  2,  3, 19,  4, 20, 21, });
	uint8x16_t const st9b = vqtbl2q_u8(st8, (uint8x16_t) { 16, 17, 18, 23,  7, 22,  5,  6, });
	uint8x16_t const st9min = vminq_u8(st9a, st9b); // [ 0], [ 1], [ 2], [ 3], [ 4], [ 5], 6, 8
	uint8x16_t const st9max = vmaxq_u8(st9a, st9b); // [15], [14], [13], [12], [11], [10], 7, 9

	uint8x16x2_t const st9 = { { st9min, st9max } };
	uint8x16_t const index = vqtbl2q_u8(st9, (uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 22, 7, 23, 21, 20, 19, 18, 17, 16 });

	uint8x16_t const res = vqtbl1q_u8(vin, index);
	vst1q_u8(output, res);
	return sizeof(uint8x16_t) + int8_t(vaddvq_u8(bmask));
}

// pruner proper, 16-batch; d-form (64-bit regs) version of testee04
inline size_t testee05() {
	uint8x16_t const vin = vld1q_u8(input);
	uint8x16_t const bmask = vcleq_u8(vin, vdupq_n_u8(' '));

	// OR the mask of all blanks with the original index of the vector
	uint8x16_t const risen = vorrq_u8(bmask, (uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });

	// now just sort that 'risen' to get the desired index of all non-blanks in the front, and all blanks in the back

	// 16-element sorting network: http://pages.ripco.net/~jgamble/nw.html -- 'Best version'
	// stage 0
	uint8x8_t const st0a = vqtbl1_u8(risen, (uint8x8_t) { 0, 2, 4, 6, 8, 10, 12, 14 });
	uint8x8_t const st0b = vqtbl1_u8(risen, (uint8x8_t) { 1, 3, 5, 7, 9, 11, 13, 15 });
	uint8x8_t const st0min = vmin_u8(st0a, st0b); //  0,  2,  4,  6,  8, 10, 12, 14
	uint8x8_t const st0max = vmax_u8(st0a, st0b); //  1,  3,  5,  7,  9, 11, 13, 15

	// stage 1
	uint8x8x2_t const st0 = { { st0min, st0max } };
	uint8x8_t const st1a = vtbl2_u8(st0, (uint8x8_t) { 0, 2, 4, 6, 8, 10, 12, 14 });
	uint8x8_t const st1b = vtbl2_u8(st0, (uint8x8_t) { 1, 3, 5, 7, 9, 11, 13, 15 });
	uint8x8_t const st1min = vmin_u8(st1a, st1b); //  0,  4,  8, 12,  1,  5,  9, 13
	uint8x8_t const st1max = vmax_u8(st1a, st1b); //  2,  6, 10, 14,  3,  7, 11, 15

	// stage 2
	uint8x8x2_t const st1 = { { st1min, st1max } };
	uint8x8_t const st2a = vtbl2_u8(st1, (uint8x8_t) { 0, 2, 4, 6, 8, 10, 12, 14 });
	uint8x8_t const st2b = vtbl2_u8(st1, (uint8x8_t) { 1, 3, 5, 7, 9, 11, 13, 15 });
	uint8x8_t const st2min = vmin_u8(st2a, st2b); //  0,  8,  1,  9,  2, 10,  3, 11
	uint8x8_t const st2max = vmax_u8(st2a, st2b); //  4, 12,  5, 13,  6, 14,  7, 15

	// stage 3
	uint8x8x2_t const st2 = { { st2min, st2max } };
	uint8x8_t const st3a = vtbl2_u8(st2, (uint8x8_t) { 0, 2, 4, 6, 8, 10, 12, 14 });
	uint8x8_t const st3b = vtbl2_u8(st2, (uint8x8_t) { 1, 3, 5, 7, 9, 11, 13, 15 });
	uint8x8_t const st3min = vmin_u8(st3a, st3b); // 0, 1,  2,  3,  4,  5,  6,  7
	uint8x8_t const st3max = vmax_u8(st3a, st3b); // 8, 9, 10, 11, 12, 13, 14, 15

	// from here on some indices are already done -- freeze them, by keeping them in deterministic positions

	// stage 4; indices done so far: 0, 15
	uint8x8x2_t const st3 = { { st3min, st3max } };
	uint8x8_t const st4a = vtbl2_u8(st3, (uint8x8_t) {  0,  5,  6,  3, 13,  7,  1,  4 });
	uint8x8_t const st4b = vtbl2_u8(st3, (uint8x8_t) { 15, 10,  9, 12, 14, 11,  2,  8 });
	uint8x8_t const st4min = vmin_u8(st4a, st4b); // [ 0],  5,  6,  3, 13,  7,  1,  4
	uint8x8_t const st4max = vmax_u8(st4a, st4b); // [15], 10,  9, 12, 14, 11,  2,  8

	// stage 5; done so far: 0, 15; temp frozen: 3, 12
	uint8x8x2_t const st4 = { { st4min, st4max } };
	uint8x8_t const st5a = vtbl2_u8(st4, (uint8x8_t) {  0,    3, 6,  5, 14, 13, 1, 10 });
	uint8x8_t const st5b = vtbl2_u8(st4, (uint8x8_t) {  8,   11, 7,  4, 15, 12, 2,  9 });
	uint8x8_t const st5min = vmin_u8(st5a, st5b); // [ 0], [ 3], 1,  7,  2, 11, 5,  9
	uint8x8_t const st5max = vmax_u8(st5a, st5b); // [15], [12], 4, 13,  8, 14, 6, 10

	// stage 6; done so far: 0, 1, 14, 15; temp frozen: 5, 6, 9, 10
	uint8x8x2_t const st5 = { { st5min, st5max } };
	uint8x8_t const st6a = vtbl2_u8(st5, (uint8x8_t) {  0,   2,  4,  5,  1,  3,  6,    7 });
	uint8x8_t const st6b = vtbl2_u8(st5, (uint8x8_t) {  8,  13, 10, 11, 12,  9, 14,   15 });
	uint8x8_t const st6min = vmin_u8(st6a, st6b); // [ 0], [ 1], 2, 11,  3,  7, [5], [ 9]
	uint8x8_t const st6max = vmax_u8(st6a, st6b); // [15], [14], 4, 13,  8, 12, [6], [10]

	// stage 7; done so far: 0, 1, 2, 13, 14, 15; temp frozen: 4, 11
	uint8x8x2_t const st6 = { { st6min, st6max } };
	uint8x8_t const st7a = vtbl2_u8(st6, (uint8x8_t) {  0,   1,    2,    3, 14, 15, 4, 5 });
	uint8x8_t const st7b = vtbl2_u8(st6, (uint8x8_t) {  8,   9,   10,   11, 12, 13, 6, 7 });
	uint8x8_t const st7min = vmin_u8(st7a, st7b); // [ 0], [ 1], [2], [11],  6, 10, 3, 7
	uint8x8_t const st7max = vmax_u8(st7a, st7b); // [15], [14], [4], [13],  8, 12, 5, 9

	// stage 8; done so far: 0, 1, 2, 13, 14, 15
	uint8x8x2_t const st7 = { { st7min, st7max } };
	uint8x8_t const st8a = vtbl2_u8(st7, (uint8x8_t) {  0,   1,    2,   6, 14,  7, 15,  3 });
	uint8x8_t const st8b = vtbl2_u8(st7, (uint8x8_t) {  8,   9,   11,  10,  4, 12,  5, 13 });
	uint8x8_t const st8min = vmin_u8(st8a, st8b); // [ 0], [ 1], [ 2],  3,  5,  7,  9, 11
	uint8x8_t const st8max = vmax_u8(st8a, st8b); // [15], [14], [13],  4,  6,  8, 10, 12

	// stage 9; done so far: 0, 1, 2, 3, 4, 5, 10, 11, 12, 13, 14, 15
	uint8x8x2_t const st8 = { { st8min, st8max } };
	uint8x8_t const st9a = vtbl2_u8(st8, (uint8x8_t) {  0,    1,    2,    3,   11,    4, 12, 13 });
	uint8x8_t const st9b = vtbl2_u8(st8, (uint8x8_t) {  8,    9,   10,   15,    7,   14,  5,  6 });
	uint8x8_t const st9min = vmin_u8(st9a, st9b); // [ 0], [ 1], [ 2], [ 3], [ 4], [ 5],  6,  8
	uint8x8_t const st9max = vmax_u8(st9a, st9b); // [15], [14], [13], [12], [11], [10],  7,  9

	uint8x16_t const st9 = vcombine_u8(st9min, st9max);
	uint8x16_t const index = vqtbl1q_u8(st9, (uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 14, 7, 15, 13, 12, 11, 10, 9, 8 });

	uint8x16_t const res = vqtbl1q_u8(vin, index);
	vst1q_u8(output, res);
	return sizeof(uint8x16_t) + int8_t(vaddvq_u8(bmask));
}

// pruner proper, 16-batch; replicates testee04/amd64
inline size_t testee06() {
	uint8x16_t const vin = vld1q_u8(input);
	uint8x16_t const bmask = vcleq_u8(vin, vdupq_n_u8(' '));

	// get the count of non-blanks for each 4-batch
	uint8x16_t const cmask = vaddq_u8(bmask, vdupq_n_u8(1));
	uint8x16_t const lena = vpaddq_u8(cmask, cmask);
	uint8x16_t const lenb = vpaddq_u8(lena, lena);
	size_t const len0 = vgetq_lane_u8(lenb, 0);
	size_t const len1 = vgetq_lane_u8(lenb, 1);
	size_t const len2 = vgetq_lane_u8(lenb, 2);
	size_t const len3 = vgetq_lane_u8(lenb, 3);

	// OR the mask of all blanks with the original index of the vector
	uint8x16_t const risen = vorrq_u8(bmask, (uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });

	// now just sort that 'risen' to get the desired index of all non-blanks in the front, and all blanks in the back;
	// an observation: we don't need to sort the entire risen index as a whole, we can sort it piece-wise

	// 4-element sorting network: http://pages.ripco.net/~jgamble/nw.html -- 'Best version', 4 clusters of
	//
	//  [[0,1],[2,3]]  [[4,5],[6,7]]  [[8,9],[a,b]]  [[c,d],[e,f]]
	//  [[0,2],[1,3]]  [[4,6],[5,7]]  [[8,a],[9,b]]  [[c,e],[d,f]]
	//  [[1,2]]        [[5,6]]        [[9,a]]        [[d,e]]
	//

	uint8x8_t const st0a = vget_low_u8(vuzp1q_u8(risen, risen));
	uint8x8_t const st0b = vget_low_u8(vuzp2q_u8(risen, risen));
	uint8x8_t const st0min = vmin_u8(st0a, st0b); // 0, 2, 4, 6, 8, a, c, e
	uint8x8_t const st0max = vmax_u8(st0a, st0b); // 1, 3, 5, 7, 9, b, d, f

	uint8x8_t const st1a = vtrn1_u8(st0min, st0max);
	uint8x8_t const st1b = vtrn2_u8(st0min, st0max);
	uint8x8_t const st1min = vmin_u8(st1a, st1b); // 0, 1, 4, 5, 8, 9, c, d
	uint8x8_t const st1max = vmax_u8(st1a, st1b); // 2, 3, 6, 7, a, b, e, f

	uint8x8_t const st2a =           st1min;
	uint8x8_t const st2b = vrev16_u8(st1max);
	uint8x8_t const st2min = vmin_u8(st2a, st2b); // [0], 1, [4], 5, [8], 9, [c], d
	uint8x8_t const st2max = vmax_u8(st2a, st2b); // [3], 2, [7], 6, [b], a, [f], e

	uint8x16_t const index = vreinterpretq_u8_u16(vzip1q_u16(
		vreinterpretq_u16_u8(vcombine_u8(          st2min,  vdup_n_u8(0))),
		vreinterpretq_u16_u8(vcombine_u8(vrev16_u8(st2max), vdup_n_u8(0)))));

	uint8x16_t const res = vqtbl1q_u8(vin, index);

	*reinterpret_cast< uint32_t* >(output)                      = vgetq_lane_u32(vreinterpretq_u32_u8(res), 0);
	*reinterpret_cast< uint32_t* >(output + len0)               = vgetq_lane_u32(vreinterpretq_u32_u8(res), 1);
	*reinterpret_cast< uint32_t* >(output + len0 + len1)        = vgetq_lane_u32(vreinterpretq_u32_u8(res), 2);
	*reinterpret_cast< uint32_t* >(output + len0 + len1 + len2) = vgetq_lane_u32(vreinterpretq_u32_u8(res), 3);
	return len0 + len1 + len2 + len3;
}

// pruner proper, 32-batch; wider version of testee06
inline size_t testee07() {
	uint8x16_t const vin0 = vld1q_u8(input);
	uint8x16_t const vin1 = vld1q_u8(input + sizeof(uint8x16_t));
	uint8x16_t const bmask0 = vcleq_u8(vin0, vdupq_n_u8(' '));
	uint8x16_t const bmask1 = vcleq_u8(vin1, vdupq_n_u8(' '));

	// get the count of non-blanks for each 4-batch
	uint8x16_t const cmask0 = vaddq_u8(bmask0, vdupq_n_u8(1));
	uint8x16_t const cmask1 = vaddq_u8(bmask1, vdupq_n_u8(1));

#if SAME_LATENCY_Q_AND_D
	uint8x16_t const lena = vpaddq_u8(cmask0, cmask1);
	uint8x16_t const lenb = vpaddq_u8(lena, lena);

	size_t const len0 = vgetq_lane_u8(lenb, 0);
	size_t const len1 = vgetq_lane_u8(lenb, 1);
	size_t const len2 = vgetq_lane_u8(lenb, 2);
	size_t const len3 = vgetq_lane_u8(lenb, 3);
	size_t const len4 = vgetq_lane_u8(lenb, 4);
	size_t const len5 = vgetq_lane_u8(lenb, 5);
	size_t const len6 = vgetq_lane_u8(lenb, 6);
	size_t const len7 = vgetq_lane_u8(lenb, 7);

#else // when q-form of the instruction comes at extra latency (e.g. A72) use d-form instead, doubling the op count but utilizing co-issue for a net reduced latency
	uint8x8_t const lena0 = vpadd_u8(vget_low_u8(cmask0), vget_high_u8(cmask0));
	uint8x8_t const lena1 = vpadd_u8(vget_low_u8(cmask1), vget_high_u8(cmask1));
	uint8x8_t const lenb0 = vpadd_u8(lena0, lena0);
	uint8x8_t const lenb1 = vpadd_u8(lena1, lena1);

	size_t const len0 = vget_lane_u8(lenb0, 0);
	size_t const len1 = vget_lane_u8(lenb0, 1);
	size_t const len2 = vget_lane_u8(lenb0, 2);
	size_t const len3 = vget_lane_u8(lenb0, 3);
	size_t const len4 = vget_lane_u8(lenb1, 0);
	size_t const len5 = vget_lane_u8(lenb1, 1);
	size_t const len6 = vget_lane_u8(lenb1, 2);
	size_t const len7 = vget_lane_u8(lenb1, 3);

#endif
	// OR the mask of all blanks with the original index of the vector
	uint8x16_t const risen0 = vorrq_u8(bmask0, (uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });
	uint8x16_t const risen1 = vorrq_u8(bmask1, (uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });

	// now just sort that 'risen' to get the desired index of all non-blanks in the front, and all blanks in the back;
	// an observation: we don't need to sort the entire risen index as a whole, we can sort it piece-wise

	// 4-element sorting network: http://pages.ripco.net/~jgamble/nw.html -- 'Best version', 4 clusters of
	//
	//  [[0,1],[2,3]]  [[4,5],[6,7]]  [[8,9],[a,b]]  [[c,d],[e,f]]
	//  [[0,2],[1,3]]  [[4,6],[5,7]]  [[8,a],[9,b]]  [[c,e],[d,f]]
	//  [[1,2]]        [[5,6]]        [[9,a]]        [[d,e]]
	//

	uint8x16_t const st0a = vuzp1q_u8(risen0, risen1);
	uint8x16_t const st0b = vuzp2q_u8(risen0, risen1);
	uint8x16_t const st0min = vminq_u8(st0a, st0b); // 0, 2, 4, 6, 8, a, c, e
	uint8x16_t const st0max = vmaxq_u8(st0a, st0b); // 1, 3, 5, 7, 9, b, d, f

	uint8x16_t const st1a = vtrn1q_u8(st0min, st0max);
	uint8x16_t const st1b = vtrn2q_u8(st0min, st0max);
	uint8x16_t const st1min = vminq_u8(st1a, st1b); // 0, 1, 4, 5, 8, 9, c, d
	uint8x16_t const st1max = vmaxq_u8(st1a, st1b); // 2, 3, 6, 7, a, b, e, f

	uint8x16_t const st2a =            st1min;
	uint8x16_t const st2b = vrev16q_u8(st1max);
	uint8x16_t const st2min = vminq_u8(st2a, st2b); // [0], 1, [4], 5, [8], 9, [c], d
	uint8x16_t const st2max = vmaxq_u8(st2a, st2b); // [3], 2, [7], 6, [b], a, [f], e

	uint16x8_t const stx = vreinterpretq_u16_u8(           st2min );
	uint16x8_t const sty = vreinterpretq_u16_u8(vrev16q_u8(st2max));

	uint8x16_t const index0 = vreinterpretq_u8_u16(vzip1q_u16(stx, sty));
	uint8x16_t const index1 = vreinterpretq_u8_u16(vzip2q_u16(stx, sty));

	uint8x16_t const res0 = vqtbl1q_u8(vin0, index0);
	uint8x16_t const res1 = vqtbl1q_u8(vin1, index1);

	// note: following len cascade is a prime candidate for implementation via prefix sum, but so far the scalar additions pipeline well
	*reinterpret_cast< uint32_t* >(output)                                                  = vgetq_lane_u32(vreinterpretq_u32_u8(res0), 0);
	*reinterpret_cast< uint32_t* >(output + len0)                                           = vgetq_lane_u32(vreinterpretq_u32_u8(res0), 1);
	*reinterpret_cast< uint32_t* >(output + len0 + len1)                                    = vgetq_lane_u32(vreinterpretq_u32_u8(res0), 2);
	*reinterpret_cast< uint32_t* >(output + len0 + len1 + len2)                             = vgetq_lane_u32(vreinterpretq_u32_u8(res0), 3);

	*reinterpret_cast< uint32_t* >(output + len0 + len1 + len2 + len3)                      = vgetq_lane_u32(vreinterpretq_u32_u8(res1), 0);
	*reinterpret_cast< uint32_t* >(output + len0 + len1 + len2 + len3 + len4)               = vgetq_lane_u32(vreinterpretq_u32_u8(res1), 1);
	*reinterpret_cast< uint32_t* >(output + len0 + len1 + len2 + len3 + len4 + len5)        = vgetq_lane_u32(vreinterpretq_u32_u8(res1), 2);
	*reinterpret_cast< uint32_t* >(output + len0 + len1 + len2 + len3 + len4 + len5 + len6) = vgetq_lane_u32(vreinterpretq_u32_u8(res1), 3);

	return len0 + len1 + len2 + len3 + len4 + len5 + len6 + len7;
}

#if defined(__ARM_FEATURE_SVE)
// scatter-enabled version of testee01, 64-batch on sve512
inline size_t testee08() {
	svbool_t const pr = svptrue_pat_b8(SV_VL64); // assumed at least sve512

	svuint8_t const vinput = svld1_u8(pr, input);
	svbool_t const pr_keep = svcmpgt_n_u8(pr, vinput, ' ');
	size_t const kept = svcntp_b8(pr_keep, pr_keep);

	// prefix sum of to-keep mask
	svuint8_t prfsum = svdup_n_u8_z(pr_keep, 1);
	prfsum = svadd_u8_x(pr, prfsum, svext_u8(svdup_n_u8(0), prfsum, 64 -  1)); // assumed exactly sve512
	prfsum = svadd_u8_x(pr, prfsum, svext_u8(svdup_n_u8(0), prfsum, 64 -  2)); // TODO: possible to use with VLEN?
	prfsum = svadd_u8_x(pr, prfsum, svext_u8(svdup_n_u8(0), prfsum, 64 -  4));
	prfsum = svadd_u8_x(pr, prfsum, svext_u8(svdup_n_u8(0), prfsum, 64 -  8));
	prfsum = svadd_u8_x(pr, prfsum, svext_u8(svdup_n_u8(0), prfsum, 64 - 16));
	prfsum = svadd_u8_x(pr, prfsum, svext_u8(svdup_n_u8(0), prfsum, 64 - 32));
	prfsum = svsub_u8_x(pr, prfsum, svdup_n_u8(1)); // 0-based prefix sum

	// 8-bit pred -> 32-bit pred
	svbool_t const pr_keep0 = svunpklo_b(svunpklo_b(pr_keep)); // - - - +
	svbool_t const pr_keep1 = svunpkhi_b(svunpklo_b(pr_keep)); // - - + -
	svbool_t const pr_keep2 = svunpklo_b(svunpkhi_b(pr_keep)); // - + - -
	svbool_t const pr_keep3 = svunpkhi_b(svunpkhi_b(pr_keep)); // + - - -

	// 8-bit chars -> 32-bit chars
	svuint32_t const winput0 = svunpklo_u32(svunpklo_u16(vinput));
	svuint32_t const winput1 = svunpkhi_u32(svunpklo_u16(vinput));
	svuint32_t const winput2 = svunpklo_u32(svunpkhi_u16(vinput));
	svuint32_t const winput3 = svunpkhi_u32(svunpkhi_u16(vinput));

	// 8-bit offsets -> 32-bit offsets
	svuint32_t const woffset0 = svunpklo_u32(svunpklo_u16(prfsum));
	svuint32_t const woffset1 = svunpkhi_u32(svunpklo_u16(prfsum));
	svuint32_t const woffset2 = svunpklo_u32(svunpkhi_u16(prfsum));
	svuint32_t const woffset3 = svunpkhi_u32(svunpkhi_u16(prfsum));

	if (svptest_any(pr_keep0, pr_keep0))
		svst1b_scatter_offset(pr_keep0, output, woffset0, winput0);

	if (svptest_any(pr_keep1, pr_keep1))
		svst1b_scatter_offset(pr_keep1, output, woffset1, winput1);

	if (svptest_any(pr_keep2, pr_keep2))
		svst1b_scatter_offset(pr_keep2, output, woffset2, winput2);

	if (svptest_any(pr_keep3, pr_keep3))
		svst1b_scatter_offset(pr_keep3, output, woffset3, winput3);

	return kept;
}

#endif
#elif __SSSE3__ && __POPCNT__
// pruner proper, 16-batch; amd64 cannot properly recreate arm64's testee04, so get creative
inline size_t testee04() {
	__m128i const vin = _mm_load_si128(reinterpret_cast< __m128i const* >(input));
	__m128i const bmask = _mm_cmplt_epi8(vin, _mm_set1_epi8(' ' + 1));

	// OR the mask of all blanks with the original index of the vector
	__m128i const risen = _mm_or_si128(bmask, _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));

	// now just sort that 'risen' to get the desired index of all non-blanks in the front, and all blanks in the back;
	// an observation: we don't need to sort the entire risen index as a whole, we can sort it piece-wise

	// 4-element sorting network: http://pages.ripco.net/~jgamble/nw.html -- 'Best version', 4 clusters of
	//
	//  [[0,1],[2,3]]  [[4,5],[6,7]]  [[8,9],[a,b]]  [[c,d],[e,f]]
	//  [[0,2],[1,3]]  [[4,6],[5,7]]  [[8,a],[9,b]]  [[c,e],[d,f]]
	//  [[1,2]]        [[5,6]]        [[9,a]]        [[d,e]]
	//

	__m128i const st0a = _mm_shuffle_epi8(risen, _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st0b = _mm_shuffle_epi8(risen, _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st0min = _mm_min_epu8(st0a, st0b); // 0, 2, 4, 6, 8, a, c, e
	__m128i const st0max = _mm_max_epu8(st0a, st0b); // 1, 3, 5, 7, 9, b, d, f

	__m128i const st0 = _mm_unpacklo_epi64(st0min, st0max);
	__m128i const st1a = _mm_shuffle_epi8(st0, _mm_setr_epi8(0, 8, 2, 10, 4, 12, 6, 14, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st1b = _mm_shuffle_epi8(st0, _mm_setr_epi8(1, 9, 3, 11, 5, 13, 7, 15, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st1min = _mm_min_epu8(st1a, st1b); // 0, 1, 4, 5, 8, 9, c, d
	__m128i const st1max = _mm_max_epu8(st1a, st1b); // 2, 3, 6, 7, a, b, e, f

	__m128i const st2a =                  st1min;
	__m128i const st2b = _mm_shuffle_epi8(st1max, _mm_setr_epi8(1, 0, 3, 2, 5, 4, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st2min = _mm_min_epu8(st2a, st2b); // [0], 1, [4], 5, [8], 9, [c], d
	__m128i const st2max = _mm_max_epu8(st2a, st2b); // [3], 2, [7], 6, [b], a, [f], e

	__m128i const st2 = _mm_unpacklo_epi64(st2min, st2max);
	__m128i const index = _mm_shuffle_epi8(st2, _mm_setr_epi8(0, 1, 9, 8, 2, 3, 11, 10, 4, 5, 13, 12, 6, 7, 15, 14));

	__m128i const res0 = _mm_shuffle_epi8(vin, index);
	__m128i const res1 = _mm_shuffle_epi32(res0, 0x55);
	__m128i const res2 = _mm_shuffle_epi32(res0, 0xee);
	__m128i const res3 = _mm_shuffle_epi32(res0, 0xff);

	uint32_t const bitmask = ~_mm_movemask_epi8(bmask);
	uint32_t const len0 = _mm_popcnt_u32(bitmask & 0x00f);
	uint32_t const len1 = _mm_popcnt_u32(bitmask & 0x0ff);
	uint32_t const len2 = _mm_popcnt_u32(bitmask & 0xfff);

	*reinterpret_cast< uint32_t* >(output)        = _mm_cvtsi128_si32(res0);
	*reinterpret_cast< uint32_t* >(output + len0) = _mm_cvtsi128_si32(res1);
	*reinterpret_cast< uint32_t* >(output + len1) = _mm_cvtsi128_si32(res2);
	*reinterpret_cast< uint32_t* >(output + len2) = _mm_cvtsi128_si32(res3);
	return _mm_popcnt_u32(bitmask & 0xffff);
}

// pruner proper, 16-batch
inline size_t testee05() {
	__m128i const vin = _mm_load_si128(reinterpret_cast< __m128i const* >(input));
	__m128i const bmask = _mm_cmplt_epi8(vin, _mm_set1_epi8(' ' + 1));

	// OR the mask of all blanks with the original index of the vector
	__m128i const risen = _mm_or_si128(bmask, _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));

	// now just sort that 'risen' to get the desired index of all non-blanks in the front, and all blanks in the back

	// 16-element sorting network: http://pages.ripco.net/~jgamble/nw.html -- 'Best version'
	// stage 0
	__m128i const st0a = _mm_shuffle_epi8(risen, _mm_setr_epi8( 0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st0b = _mm_shuffle_epi8(risen, _mm_setr_epi8( 1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st0min = _mm_min_epu8(st0a, st0b); //  0, 2, 4, 6, 8, 10, 12, 14
	__m128i const st0max = _mm_max_epu8(st0a, st0b); //  1, 3, 5, 7, 9, 11, 13, 15

	// stage 1
	__m128i const st0 = _mm_unpacklo_epi64(st0min, st0max);
	__m128i const st1a = _mm_shuffle_epi8(st0, _mm_setr_epi8( 0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st1b = _mm_shuffle_epi8(st0, _mm_setr_epi8( 1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st1min = _mm_min_epu8(st1a, st1b); //  0, 4,  8, 12, 1, 5,  9, 13
	__m128i const st1max = _mm_max_epu8(st1a, st1b); //  2, 6, 10, 14, 3, 7, 11, 15

	// stage 2
	__m128i const st1 = _mm_unpacklo_epi64(st1min, st1max);
	__m128i const st2a = _mm_shuffle_epi8(st1, _mm_setr_epi8( 0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st2b = _mm_shuffle_epi8(st1, _mm_setr_epi8( 1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st2min = _mm_min_epu8(st2a, st2b); //  0,  8, 1,  9, 2, 10, 3, 11
	__m128i const st2max = _mm_max_epu8(st2a, st2b); //  4, 12, 5, 13, 6, 14, 7, 15

	// stage 3
	__m128i const st2 = _mm_unpacklo_epi64(st2min, st2max);
	__m128i const st3a = _mm_shuffle_epi8(st2, _mm_setr_epi8( 0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st3b = _mm_shuffle_epi8(st2, _mm_setr_epi8( 1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st3min = _mm_min_epu8(st3a, st3b); // 0, 1,  2,  3,  4,  5,  6,  7
	__m128i const st3max = _mm_max_epu8(st3a, st3b); // 8, 9, 10, 11, 12, 13, 14, 15

	// from here on some indices are already done -- freeze them, by keeping them in deterministic positions

	// stage 4; indices done so far: 0, 15
	__m128i const st3 = _mm_unpacklo_epi64(st3min, st3max);
	__m128i const st4a = _mm_shuffle_epi8(st3, _mm_setr_epi8( 0,  5, 6,  3, 13,  7, 1, 4, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st4b = _mm_shuffle_epi8(st3, _mm_setr_epi8(15, 10, 9, 12, 14, 11, 2, 8, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st4min = _mm_min_epu8(st4a, st4b); // [ 0],  5,  6,  3, 13,  7,  1,  4
	__m128i const st4max = _mm_max_epu8(st4a, st4b); // [15], 10,  9, 12, 14, 11,  2,  8

	// stage 5; done so far: 0, 15; temp frozen: 3, 12
	__m128i const st4 = _mm_unpacklo_epi64(st4min, st4max);
	__m128i const st5a = _mm_shuffle_epi8(st4, _mm_setr_epi8( 0,  3, 6, 5, 14, 13, 1, 10, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st5b = _mm_shuffle_epi8(st4, _mm_setr_epi8( 8, 11, 7, 4, 15, 12, 2,  9, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st5min = _mm_min_epu8(st5a, st5b); // [ 0], [ 3], 1,  7,  2, 11, 5,  9
	__m128i const st5max = _mm_max_epu8(st5a, st5b); // [15], [12], 4, 13,  8, 14, 6, 10

	// stage 6; done so far: 0, 1, 14, 15; temp frozen: 5, 6, 9, 10
	__m128i const st5 = _mm_unpacklo_epi64(st5min, st5max);
	__m128i const st6a = _mm_shuffle_epi8(st5, _mm_setr_epi8( 0,  2,  4,  5,  1, 3,  6,  7, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st6b = _mm_shuffle_epi8(st5, _mm_setr_epi8( 8, 13, 10, 11, 12, 9, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st6min = _mm_min_epu8(st6a, st6b); // [ 0], [ 1], 2, 11,  3,  7, [5], [ 9]
	__m128i const st6max = _mm_max_epu8(st6a, st6b); // [15], [14], 4, 13,  8, 12, [6], [10]

	// stage 7; done so far: 0, 1, 2, 13, 14, 15; temp frozen: 4, 11
	__m128i const st6 = _mm_unpacklo_epi64(st6min, st6max);
	__m128i const st7a = _mm_shuffle_epi8(st6, _mm_setr_epi8( 0, 1,  2,  3, 14, 15, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st7b = _mm_shuffle_epi8(st6, _mm_setr_epi8( 8, 9, 10, 11, 12, 13, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st7min = _mm_min_epu8(st7a, st7b); // [ 0], [ 1], [2], [11],  6, 10, 3, 7
	__m128i const st7max = _mm_max_epu8(st7a, st7b); // [15], [14], [4], [13],  8, 12, 5, 9

	// stage 8; done so far: 0, 1, 2, 13, 14, 15
	__m128i const st7 = _mm_unpacklo_epi64(st7min, st7max);
	__m128i const st8a = _mm_shuffle_epi8(st7, _mm_setr_epi8( 0, 1,  2,  6, 14,  7, 15,  3, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st8b = _mm_shuffle_epi8(st7, _mm_setr_epi8( 8, 9, 11, 10,  4, 12,  5, 13, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st8min = _mm_min_epu8(st8a, st8b); // [ 0], [ 1], [ 2],  3,  5,  7,  9, 11
	__m128i const st8max = _mm_max_epu8(st8a, st8b); // [15], [14], [13],  4,  6,  8, 10, 12

	// stage 9; done so far: 0, 1, 2, 3, 4, 5, 10, 11, 12, 13, 14, 15
	__m128i const st8 = _mm_unpacklo_epi64(st8min, st8max);
	__m128i const st9a = _mm_shuffle_epi8(st8, _mm_setr_epi8( 0, 1,  2,  3, 11,  4, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st9b = _mm_shuffle_epi8(st8, _mm_setr_epi8( 8, 9, 10, 15,  7, 14,  5,  6, -1, -1, -1, -1, -1, -1, -1, -1));
	__m128i const st9min = _mm_min_epu8(st9a, st9b); // [ 0], [ 1], [ 2], [ 3], [ 4], [ 5],  6,  8
	__m128i const st9max = _mm_max_epu8(st9a, st9b); // [15], [14], [13], [12], [11], [10],  7,  9

	__m128i const st9 = _mm_unpacklo_epi64(st9min, st9max);
	__m128i const index = _mm_shuffle_epi8(st9, _mm_setr_epi8( 0, 1, 2, 3, 4, 5, 6, 14, 7, 15, 13, 12, 11, 10, 9, 8 ));

	__m128i const res = _mm_shuffle_epi8(vin, index);
	_mm_storeu_si128(reinterpret_cast< __m128i* >(output), res);
	return sizeof(__m128i) - _mm_popcnt_u32(_mm_movemask_epi8(bmask));
}

#endif
int main(int, char**) {
	size_t const rep = size_t(5e7);

	for (size_t i = 0; i < rep; ++i) {

#if TESTEE == 8 && defined(__ARM_FEATURE_SVE)
		testee08();

#elif TESTEE == 7
		testee07();

#elif TESTEE == 6
		testee06();

#elif TESTEE == 5
		testee05();

#elif TESTEE == 4
		testee04();

#elif TESTEE == 3
		testee03();

#elif TESTEE == 2
		testee02();

#elif TESTEE == 1
		testee01();

#else
		testee00();

#endif
		// iteration obfuscator
		asm volatile ("" : : : "memory");
	}

	fprintf(stderr, "%.32s\n", output);
	return 0;
}

