#include "pch.h"

_NT_BEGIN

#include "task.h"

CTask::~CTask()
{
	DbgPrint("[%x]: [%u] %s<%p>\n", GetCurrentThreadId(), GetTickCount(), __FUNCTION__, this);
	if (_pHF) _pHF->Release();
	if (_hAlgorithm) BCryptCloseAlgorithmProvider(_hAlgorithm, 0);
	if (_hFile) NtClose(_hFile);
}

void CTask::StartIo()
{
	InterlockedIncrementNoFence(&_nIoCount);
}

void CTask::EndIo()
{
	if (!InterlockedDecrement(&_nIoCount))
	{
		DbgPrint("[%x]: [%u] %s<%p>\n", GetCurrentThreadId(), GetTickCount(), __FUNCTION__, this);
		if (_pHF) _pHF->Complete(), _pHF->Release(), _pHF = 0;
		Release();
	}
}

void CTask::Start(UCHAR nIoCount)
{
	if (_nFragments < nIoCount)
	{
		nIoCount = (UCHAR)_nFragments;
	}

	_nIoCount = 1;
	AddRef();

	do 
	{
		if (CFragment* p = new CFragment(this))
		{
			if (0 <= p->Create())
			{
				ReUseBlock(p);
			}
			p->Release();
		}
	} while (--nIoCount);

	EndIo();
}

NTSTATUS CTask::Create(HWND hwnd, PCWSTR pszFileName, PCWSTR pszHashFile, PCWSTR pszAlgId, ULONGLONG cbBlock, BOOL bCachedIo)
{
	if (cbBlock < 0)
	{
		return STATUS_INVALID_PARAMETER;
	}

	UNICODE_STRING ObjectName;
	NTSTATUS status = RtlDosPathNameToNtPathName_U_WithStatus(pszFileName, &ObjectName, 0, 0);

	if (0 <= status)
	{
		OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName };

		IO_STATUS_BLOCK iosb;

		status = NtOpenFile(&oa.RootDirectory, FILE_READ_DATA, &oa, &iosb, 
			FILE_SHARE_READ, bCachedIo ? 
			FILE_NON_DIRECTORY_FILE|FILE_SEQUENTIAL_ONLY : FILE_NON_DIRECTORY_FILE|FILE_NO_INTERMEDIATE_BUFFERING);

		RtlFreeUnicodeString(&ObjectName);

		if (0 <= status)
		{
			LARGE_INTEGER eof;

			ULONG SectorSize, AlignmentRequirement;

			if (0 <= (status = GetSectorSizeAndAligment(oa.RootDirectory, AlignmentRequirement, SectorSize, &eof)))
			{
				if (eof.QuadPart)
				{
					if (bCachedIo)
					{
						SectorSize = 1, AlignmentRequirement = 0;
					}

					enum { _64kb = 0x10000, _1Mb = 0x100000 };

					_AlignmentRequirement = AlignmentRequirement < sizeof(PVOID) ? 0 : AlignmentRequirement;
					_SectorSize = SectorSize;
					_cbBlock = cbBlock;
					_cbIo = SectorSize < _64kb ? roundUp((ULONG)_64kb, SectorSize) : SectorSize;
					_cbFragment = cbBlock < _1Mb ? roundUp((ULONGLONG)_1Mb, cbBlock) : cbBlock;
					_nFragments = (eof.QuadPart + _cbFragment - 1) / _cbFragment;
					_FileSize = eof.QuadPart;

					SetBytesToProcess(eof.QuadPart);

					_dwBytesInLastRead = (eof.QuadPart - roundDown(roundDown(eof.QuadPart, _cbFragment), SectorSize)) % _cbIo;

					BCRYPT_ALG_HANDLE hAlgorithm;

					if (0 <= (status = BCryptOpenAlgorithmProvider(&hAlgorithm, pszAlgId, MS_PRIMITIVE_PROVIDER, 0)))
					{
						ULONG cbHash, cbResult;

						if (0 <= (status = BCryptGetProperty(hAlgorithm, BCRYPT_HASH_LENGTH, (PUCHAR)&cbHash, sizeof(cbHash), &cbResult, 0)))
						{
							if (CHashFile* pHF = new CHashFile(hwnd))//(ULONGLONG)
							{
								if (0 <= (status = pHF->Create(pszHashFile, (eof.QuadPart + cbBlock - 1) / cbBlock, cbHash)))
								{
									if (0 <= (status = RtlSetIoCompletionCallback(oa.RootDirectory, CFragment::_IOCompletionRoutine, 0)))
									{
										_hFile = oa.RootDirectory;
										_pHF = pHF;
										_hAlgorithm = hAlgorithm;

										return STATUS_SUCCESS;
									}

									pHF->Release();
								}
							}
							else
							{
								status = STATUS_INSUFFICIENT_RESOURCES;
							}
						}

						BCryptCloseAlgorithmProvider(hAlgorithm, 0);
					}
				}
			}

			NtClose(oa.RootDirectory);
		}
	}

	return status;
}

void CTask::ReUseBlock(CFragment* pBlock)
{
	LONGLONG iFragment = InterlockedDecrement64(&_nFragments);

	if (0 <= iFragment)
	{
		pBlock->Start(iFragment);
	}
}

void CTask::SetBytesToProcess(ULONGLONG FileSize)
{

	ULONG SectorSize = _SectorSize;

	ULONGLONG cbFrarment = _cbFragment;

	if (cbFrarment % SectorSize)
	{
		ULONGLONG iFragment = _nFragments;

		ULONGLONG a = 0;
		do 
		{
			if (a % SectorSize)
			{
				FileSize += SectorSize;
			}

		} while (a += cbFrarment, --iFragment);
	}

	_dwBytesToProcess = FileSize;
}

void CFragment::Read()
{
	CTask* pTask = _pTask;

	AddRef();

	ULONGLONG Length = _Length;
	ULONG cbIo = pTask->_cbIo;

	NTSTATUS status = STATUS_CANCELLED;

	if (pTask->AcquireRP())
	{
		status = NtReadFile(pTask->_hFile, 0, 0, this, this, GetIoBuffer(), (ULONG)min(Length, cbIo), &_ByteOffset, 0);
		pTask->ReleaseRP();
	}

	if (NT_ERROR(status))
	{
		IOCompletionRoutine(status, 0);
	}
}

NTSTATUS CFragment::Create()
{
	CTask* pTask = _pTask;

	ULONG AlignmentRequirement = pTask->_AlignmentRequirement;

	if (PBYTE pb = new UCHAR[AlignmentRequirement + pTask->_cbIo])
	{
		_pbBuf = pb;

		return STATUS_SUCCESS;
	}

	return STATUS_INSUFFICIENT_RESOURCES;
}

void CFragment::Start(LONGLONG iFragment)
{
	//DbgPrint("[%x]: %s<%p>(%I64x)----------\n", GetCurrentThreadId(), __FUNCTION__, this, iFragment);

	CTask* pTask = _pTask;

	ULONG SectorSize = pTask->_SectorSize;

	ULONGLONG cbFrarment = pTask->_cbFragment;

	ULONGLONG a = iFragment * cbFrarment, b = a + cbFrarment, FileSize = pTask->_FileSize;

	_x = a, _dwBlock = pTask->_cbBlock;

	_ByteOffset.QuadPart = a = roundDown(a, SectorSize);

	if (b > FileSize)
	{
		b = FileSize;
	}

	_Length = roundUp(b, SectorSize) - a;

	_nHashesToComplete = (ULONG)(cbFrarment / _dwBlock);

	Read();
}

CFragment::CFragment(CTask* pTask) : _pTask(pTask), _hHash(0), _pbBuf(0), _pHashBlock(0)
{
	DbgPrint("[%x]: [%u] %s<%p>\n", GetCurrentThreadId(), GetTickCount(), __FUNCTION__, this);
	pTask->StartIo();
}

CFragment::~CFragment()
{
	DbgPrint("[%x]: [%u] %s<%p>\n", GetCurrentThreadId(), GetTickCount(), __FUNCTION__, this);

	if (_pHashBlock) _pTask->_pHF->CompleteHash(_pHashBlock);

	if (_hHash) BCryptDestroyHash(_hHash);

	if (_pbBuf) delete [] _pbBuf;

	_pTask->EndIo();
}

void CFragment::IOCompletionRoutine(NTSTATUS status, ULONG dwNumberOfBytesTransfered)
{
	//DbgPrint("%s<%p>(%x %x/%I64x) [%I64x, %I64x) %I64x>\n", __FUNCTION__, this, status, dwNumberOfBytesTransfered, _Length, _ByteOffset.QuadPart, _ByteOffset.QuadPart + dwNumberOfBytesTransfered, _ByteOffset.QuadPart + _Length);

	CTask* pTask = _pTask;

	if (0 <= status)
	{
		status = ProcessData(pTask, dwNumberOfBytesTransfered);
	}

	if (0 > status)
	{
		pTask->SetError(status);
	}
	else
	{
		InterlockedExchangeAddNoFence64(&pTask->_dwBytesToProcess, -(LONGLONG)dwNumberOfBytesTransfered);

		if (_Length)
		{
			Read();
		}
		else
		{
			pTask->ReUseBlock(this);
		}
	}

	Release();
}

NTSTATUS CFragment::ProcessData(CTask* pTask, ULONG dwNumberOfBytesTransfered)
{
	bool bLastBlock = dwNumberOfBytesTransfered < pTask->_cbIo;

	if (bLastBlock && 
		dwNumberOfBytesTransfered != _Length && 
		dwNumberOfBytesTransfered != pTask->_dwBytesInLastRead)
	{
		DbgPrint("LastRead %x != %x", dwNumberOfBytesTransfered, pTask->_dwBytesInLastRead);
		return STATUS_UNSUCCESSFUL;
	}

	PBYTE pb = GetIoBuffer();

	ULONGLONG cbBlock = pTask->_cbBlock;

	LONGLONG a = _ByteOffset.QuadPart;
	LONGLONG x = _x, iBlock = x / cbBlock;

	_ByteOffset.QuadPart += dwNumberOfBytesTransfered, _Length -= dwNumberOfBytesTransfered;

	if (x < a) __debugbreak();

	ULONGLONG dwBlock = _dwBlock;
	ULONG cb = (ULONG)(x - a);

	if (dwNumberOfBytesTransfered <= cb)
	{
		return STATUS_UNSUCCESSFUL;
	}

	pb += (ULONG_PTR)cb, dwNumberOfBytesTransfered -= cb;

	CHashFile* pHF = pTask->_pHF;

	BCRYPT_HASH_HANDLE hHash = _hHash;

	do 
	{
		if (pTask->IsRundownBegin())
		{
			return STATUS_CANCELLED;
		}

		NTSTATUS status;

		if (!hHash)
		{
			if (!(_pHashBlock = pHF->GetHashBuffer(iBlock, &_pbHash)))
			{
				return STATUS_INSUFFICIENT_RESOURCES;
			}

			if (0 > (status = BCryptCreateHash(pTask->_hAlgorithm, &hHash, 0, 0, 0, 0, 0)))
			{
				return status;
			}

			_hHash = hHash;

			//DbgPrint("<++ Hash(%I64x) [%I64x(%I64u) ...\n", iBlock, _x, _x);
		}

		cb = (ULONG)min(dwNumberOfBytesTransfered, dwBlock);

		if (0 > (status = BCryptHashData(hHash, pb, cb, 0)))
		{
			break;
		}

		//DbgPrint("HashData [%I64x, %I64x) [%I64u, %I64u) [%u]\n", _x, _x + cb, _x, _x + cb, cb);

		pb += cb, _x += cb, dwNumberOfBytesTransfered -= cb;

		if (!(dwBlock -= cb))
		{
__lastBlock:
			status = BCryptFinishHash(hHash, _pbHash, pHF->GetHashSize(), 0);

			BCryptDestroyHash(hHash), _hHash = 0, hHash = 0;

			if (0 > status)
			{
				return status;
			}

			pHF->CompleteHash(_pHashBlock), _pHashBlock = 0;

			//DbgPrint("...      %I64x(%I64u)) Hash(%I64x)-->\n", _x, _x, iBlock);

			if (!--_nHashesToComplete)
			{
				return STATUS_SUCCESS;
			}

			iBlock++;
			dwBlock = cbBlock;
		}

		_dwBlock = dwBlock;

	} while (dwNumberOfBytesTransfered);

	if (bLastBlock)
	{
		_Length = 0;

		if (hHash)
		{
			goto __lastBlock;
		}
	}

	return STATUS_SUCCESS;
}

_NT_END