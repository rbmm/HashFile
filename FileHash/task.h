#pragma once

#include "../inc/rundown.h"
#include "util.h"
#include "hash.h"

class CFragment;

class CTask : public CObjRef
{
	friend CFragment;

	ULONGLONG _cbBlock;
	ULONGLONG _cbFragment;
	LONGLONG _nFragments;
	LONGLONG _dwBytesToProcess;
	BCRYPT_ALG_HANDLE _hAlgorithm;
	CHashFile* _pHF;
	HANDLE _hFile;
	RundownProtection _HandleLock;
	ULONG _AlignmentRequirement;
	ULONG _SectorSize;
	ULONG _cbIo;
	ULONG _dwBytesInLastRead;
	LONG _nIoCount;

	virtual ~CTask();

	void StartIo();
	void EndIo();

	void SetError(NTSTATUS status)
	{
		_pHF->SetError(status);
	}

	void ReUseBlock(CFragment* pBlock);

	_NODISCARD BOOL LockHandle()
	{
		return _HandleLock.Acquire();
	}

	void UnlockHandle()
	{
		if (_HandleLock.Release())
		{
			CancelIoEx(_hFile, 0);
		}
	}

	void SetBytesToProcess(ULONGLONG FileSize);

public:

	_NODISCARD BOOL IsRundownBegin()
	{
		return _HandleLock.IsRundownBegin();
	}

	NTSTATUS IsSeekPenalty(BOOLEAN& SeekPenalty)
	{
		return GetSeekPenalty(_hFile, SeekPenalty);
	}

	ULONGLONG GetBytesToProcess()
	{
		return _dwBytesToProcess;
	}

	void Start(UCHAR nIoCount);

	void Cancel();

	NTSTATUS Create(HWND hwnd, PCWSTR pszFile, PCWSTR pszHashFile, PCWSTR pszAlgId, ULONGLONG cbBlock, BOOL bCachedIo);

	CTask() : _hAlgorithm(0), _hFile(0), _pHF(0), _HandleLock(RundownProtection::v_init)
	{
		DbgPrint("[%x]: [%u] %s<%p>\n", GetCurrentThreadId(), GetTickCount(), __FUNCTION__, this);
	}
};

class CFragment : public CObjRef, IO_STATUS_BLOCK
{
	LARGE_INTEGER _ByteOffset;
	ULONGLONG _Length;
	LONGLONG _x;// block offset
	ULONGLONG _dwBlock;// bytes left in block
	CTask* _pTask;
	PUCHAR _pbBuf;
	BCRYPT_HASH_HANDLE _hHash;
	CHashFile::PCHashBlock _pHashBlock;
	PUCHAR _pbHash;
	ULONG _nHashesToComplete;

public:

	PBYTE GetIoBuffer()
	{
		ULONG_PTR AlignmentRequirement = _pTask->_AlignmentRequirement;

		return (PBYTE)(((ULONG_PTR)_pbBuf + AlignmentRequirement) & ~AlignmentRequirement);
	}

	void Read();

	NTSTATUS Create();

	void Start(LONGLONG iFragment);

	CFragment(CTask* pTask);

	virtual ~CFragment();

	static void CALLBACK _IOCompletionRoutine(NTSTATUS status, ULONG_PTR dwNumberOfBytesTransfered, PVOID ApcContext)
	{
		// we must pass CFragment pointer in place ApcContext in I/O call
		reinterpret_cast<CFragment*>(ApcContext)->IOCompletionRoutine(status, (ULONG)dwNumberOfBytesTransfered);
	}

	void IOCompletionRoutine(NTSTATUS status, ULONG dwNumberOfBytesTransfered);

	NTSTATUS ProcessData(CTask* pTask, ULONG dwNumberOfBytesTransfered);
};
