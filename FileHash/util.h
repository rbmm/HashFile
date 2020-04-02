#pragma once

ULONG gcd(ULONG m, ULONG n);

ULONG lcm(ULONG m, ULONG n);

NTSTATUS GetSeekPenalty(HANDLE hFile, BOOLEAN& SeekPenalty);

NTSTATUS GetSectorSizeAndAligment(HANDLE hFile, ULONG& AlignmentRequirement, ULONG& SectorSize, PLARGE_INTEGER EndOfFile = 0);

template <typename T, typename U>
inline T roundUp(T numToRound, U multiple) 
{
	U remainder = numToRound % multiple;

	return remainder ? numToRound - remainder + multiple : numToRound;
}

//#define roundUp(numToRound, multiple) _roundUp<decltype(numToRound)>(numToRound, multiple)
#define roundDown(numToRound, multiple) ((numToRound) - ((numToRound) % (multiple)))