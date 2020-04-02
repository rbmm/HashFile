#include "pch.h"

_NT_BEGIN

#include "util.h"
#include "hash.h"

void CHashFile::CHashBlock::IOCompletionRoutine(NTSTATUS status)
{
	DbgPrint("[%x]: [%u] CHashBlock<%p, %I64x> completed!(%x)\n", GetCurrentThreadId(), GetTickCount(), this, _i, status);

	CHashFile* pHashFile = _pHashFile;

	if (0 > status)
	{
		pHashFile->SetError(status);
	}

	_pHashFile = 0;
	pHashFile->Release();
}

CHashFile::CHashBlock* CHashFile::GetHashBlock(LONGLONG iBlock)
{
	CHashBlock* pBlock, *pFreeBlock = 0;

	PLIST_ENTRY head = this, entry = head;

	while ((entry = entry->Blink) != head)
	{
		pBlock = static_cast<CHashBlock*>(entry);

		if (pBlock->_i == iBlock)
		{
			return pBlock;
		}

		if (!pBlock->_pHashFile)
		{
			pFreeBlock = pBlock;
		}
	}

	if (!pFreeBlock)
	{
		if (pFreeBlock = (CHashBlock*)LocalAlloc(0, sizeof(CHashBlock) + _AlignmentRequirement + _nHashesInBlock * _cbHash))
		{
			DbgPrint("[%x]: ++ HB<%p>\n", GetCurrentThreadId(), pFreeBlock);
			InsertHeadList(head, pFreeBlock);
		}
	}
	else
	{
		DbgPrint("[%x]: !! HB<%p>\n", GetCurrentThreadId(), pFreeBlock);
	}

	if (pFreeBlock)
	{
		pFreeBlock->_i = iBlock;
		pFreeBlock->_pHashFile = this;
		pFreeBlock->_nHashesToComplete = _iLastBlock == iBlock ? _nHashesInLastBlock : _nHashesInBlock;
		AddRef();
		return pFreeBlock;
	}

	return 0;
}

CHashFile::~CHashFile()
{
	DbgPrint("[%x]: %s<%p>(%I64x)\n", GetCurrentThreadId(), __FUNCTION__, this, _nBlocks);

	if (_hFile)
	{
		if (!_nBlocks)
		{
			IO_STATUS_BLOCK iosb;
			if (_eof.EndOfFile.QuadPart)
			{
				NtSetInformationFile(_hFile, &iosb, &_eof, sizeof(_eof), FileEndOfFileInformation);

				DbgPrint("set EOF=%I64x\n", _eof.EndOfFile.QuadPart);
			}

			static FILE_DISPOSITION_INFORMATION fdi = { FALSE };
			NtSetInformationFile(_hFile, &iosb, &fdi, sizeof(fdi), FileDispositionInformation);
		}
		NtClose(_hFile);
	}

	PLIST_ENTRY head = this, entry = head->Blink;
	while (entry != head)
	{
		CHashBlock* pBlock = static_cast<CHashBlock*>(entry);
		entry = entry->Blink;
		LocalFree(pBlock);
		DbgPrint("--HB<%p>\n", pBlock);
	}

	PostMessageW(_hwnd, WM_DONE, 0, _errorStatus);
}

CHashFile::CHashFile(HWND hwnd) : _hFile(0), _errorStatus(STATUS_SUCCESS), _hwnd(hwnd), _eof{}
{
	DbgPrint("[%x]: %s<%p>\n", GetCurrentThreadId(), __FUNCTION__, this);
	InitializeListHead(this);
	InitializeSRWLock(&_lock);
}

NTSTATUS CHashFile::Create(PCWSTR pszFileName, ULONGLONG nBlocks, ULONG cbHash)
{
	UNICODE_STRING ObjectName;
	NTSTATUS status = RtlDosPathNameToNtPathName_U_WithStatus(pszFileName, &ObjectName, 0, 0);

	if (0 <= status)
	{
		OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName };

		IO_STATUS_BLOCK iosb;
		LARGE_INTEGER eof;
		eof.QuadPart = nBlocks * cbHash;

		DbgPrint("HashFileSize = %I64x\n", eof.QuadPart);

		status = NtCreateFile(&oa.RootDirectory, FILE_WRITE_DATA|DELETE, &oa, &iosb, &eof, 0,
			0, FILE_OVERWRITE_IF, FILE_NON_DIRECTORY_FILE|FILE_NO_INTERMEDIATE_BUFFERING, 0, 0);

		RtlFreeUnicodeString(&ObjectName);

		if (0 <= status)
		{
			static FILE_DISPOSITION_INFORMATION fdi = { TRUE };
			NtSetInformationFile(oa.RootDirectory, &iosb, &fdi, sizeof(fdi), FileDispositionInformation);

			ULONG SectorSize;

			union {
				ULONG AlignmentRequirement;
				ULONG BlockSize;
				ULONG MinBlockSize;
				ULONG nHashesInBlock;
				ULONG LastBlockSize;
			};

			if (0 <= (status = GetSectorSizeAndAligment(oa.RootDirectory, AlignmentRequirement, SectorSize)))
			{
				_AlignmentRequirement = AlignmentRequirement < __alignof(decltype(CHashBlock::_align)) ? 0 : AlignmentRequirement;

				// calc minimum size for HashBlock
				MinBlockSize = lcm(SectorSize, cbHash); 

				ULONG minHashCount = MinBlockSize / cbHash;

				DbgPrint("MinBlockSize=%x minHashCount=%x\n", MinBlockSize, minHashCount);

				// size of hash block ( ~= 1MB)//
				nHashesInBlock = (((ULONG)min(eof.QuadPart, 0x100000) + MinBlockSize - 1) / MinBlockSize) * minHashCount;

				_cbHash = cbHash, _nHashesInBlock = nHashesInBlock; 

				_nBlocks = nBlocks;
				_iLastBlock = nBlocks / nHashesInBlock;
				_nHashesInLastBlock = nBlocks % nHashesInBlock;

				_dwLastWrite = 0;

				if (LastBlockSize = cbHash * _nHashesInLastBlock)
				{
					// (LastBlockSize + SectorSize - 1) & ~(SectorSize - 1)
					_dwLastWrite = ((LastBlockSize + SectorSize - 1) / SectorSize) * SectorSize;

					if (LastBlockSize % SectorSize)
					{
						_eof.EndOfFile.QuadPart = eof.QuadPart;
					}
				}

				DbgPrint("[%x] last=%x[%x]\n", _nHashesInBlock, _iLastBlock, _nHashesInLastBlock);

				NtSetInformationFile(oa.RootDirectory, &iosb, &eof, sizeof(eof), FileEndOfFileInformation);
				NtSetInformationFile(oa.RootDirectory, &iosb, &eof, sizeof(eof), FileValidDataLengthInformation);

				if (0 <= (status = RtlSetIoCompletionCallback(oa.RootDirectory, CHashBlock::_IOCompletionRoutine, 0)))
				{
					_hFile = oa.RootDirectory;

					return STATUS_SUCCESS;
				}
			}

			NtClose(oa.RootDirectory);
		}
	}

	return status;
}

void CHashFile::Complete()
{
	AcquireSRWLockExclusive(&_lock);

	CHashBlock* pBlock;

	PLIST_ENTRY head = this, entry = head;

	while ((entry = entry->Blink) != head)
	{
		pBlock = static_cast<CHashBlock*>(entry);

		if (pBlock->_nHashesToComplete)
		{
			// CompleteHash called only from CFragment::ProcessData or CFragment::~CFragment
			// Complete called only from CTask::EndIo when no more CFragment
			// so no more CompleteHash will be called
			// _pHashFile used only from CHashBlock::IOCompletionRoutine
			// if pBlock->_nHashesToComplete != 0 it not called
			// so we can zero _pHashFile and release self

			DbgPrint("[%x]: CHashBlock<%p>(%x) release!\n", GetCurrentThreadId(), pBlock, pBlock->_nHashesToComplete);
			pBlock->_pHashFile = 0;
			Release();
		}
		else
		{
			// CHashBlock::IOCompletionRoutine will be or already called
			DbgPrint("[%x]: CHashBlock<%p>(%p %x %p) in use!!\n", GetCurrentThreadId(), pBlock, pBlock->_pHashFile, pBlock->Status, pBlock->Information);
		}

	}

	ReleaseSRWLockExclusive(&_lock);
}

void CHashFile::CompleteHash(PCHashBlock pBlock)
{
	//DbgPrint("[%x]: %s(%p)\n", GetCurrentThreadId(), __FUNCTION__, pBlock);

	InterlockedDecrement64(&_nBlocks);

	if (!InterlockedDecrement(&pBlock->_nHashesToComplete))
	{
		// write hash block to disk
		ULONG cb = _nHashesInBlock * _cbHash;
		LONGLONG i = pBlock->_i;
		LARGE_INTEGER ByteOffset;
		ByteOffset.QuadPart = i * cb;
		if (i == _iLastBlock)
		{
			cb = _dwLastWrite;
		}

		pBlock->Status = STATUS_PENDING, pBlock->Information = 0;

		NTSTATUS status = NtWriteFile(_hFile, 0, 0, pBlock, pBlock, GetHashBuffer(pBlock), cb, &ByteOffset, 0);

		if (NT_ERROR(status))
		{
			CHashBlock::_IOCompletionRoutine(status, 0, pBlock);
		}
	}
}

CHashFile::PCHashBlock CHashFile::GetHashBuffer(ULONGLONG iBlock, PUCHAR* ppbHash)
{
	if (_errorStatus)
	{
		return 0;
	}

	AcquireSRWLockExclusive(&_lock);
	CHashBlock* pBlock = GetHashBlock(iBlock / _nHashesInBlock);
	ReleaseSRWLockExclusive(&_lock);

	if (pBlock)
	{
		*ppbHash = GetHashBuffer(pBlock) + (iBlock % _nHashesInBlock) * _cbHash;
	}

	return pBlock;
}


_NT_END