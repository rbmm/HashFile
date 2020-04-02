#pragma once

#include "objbase.h"

enum { WM_DONE = WM_USER + 0x100 };

class CHashFile : public CObjRef, LIST_ENTRY
{
	LONGLONG _iLastBlock;
	LONGLONG _nBlocks;
	FILE_END_OF_FILE_INFORMATION _eof;
	HANDLE _hFile;
	SRWLOCK _lock;
	HWND _hwnd;
	ULONG _AlignmentRequirement;
	ULONG _cbHash;
	ULONG _nHashesInBlock;
	ULONG _nHashesInLastBlock;
	ULONG _dwLastWrite;
	NTSTATUS _errorStatus;

	struct CHashBlock : LIST_ENTRY, IO_STATUS_BLOCK
	{
		LONGLONG _i;
		CHashFile* _pHashFile;
		LONG _nHashesToComplete;
		union {
			ULONG _align[];
			UCHAR _hashes[];
		};

		static void CALLBACK _IOCompletionRoutine(NTSTATUS status, ULONG_PTR /*dwNumberOfBytesTransfered*/, PVOID ApcContext)
		{
			reinterpret_cast<CHashBlock*>(ApcContext)->IOCompletionRoutine(status);
		}
		
		void IOCompletionRoutine(NTSTATUS status);
	};

	CHashBlock* GetHashBlock(LONGLONG iBlock);

	PUCHAR GetHashBuffer(CHashBlock* pBlock)
	{
		ULONG_PTR AlignmentRequirement = _AlignmentRequirement;
		
		return (PUCHAR)(((ULONG_PTR)pBlock->_hashes + AlignmentRequirement) & ~AlignmentRequirement);
	}

	virtual ~CHashFile();

public:

	typedef CHashBlock *PCHashBlock;

	CHashFile(HWND hwnd);

	NTSTATUS Create(PCWSTR pszFileName, ULONGLONG nBlocks, ULONG cbHash);
	
	void CompleteHash(PCHashBlock pBlock);

	void Complete();

	PCHashBlock GetHashBuffer(ULONGLONG iBlock, PUCHAR* ppbHash);

	void SetError(NTSTATUS status)
	{
		InterlockedCompareExchange(&_errorStatus, status, STATUS_SUCCESS);
	}

	ULONG GetHashSize()
	{
		return _cbHash;
	}
};
