#include "stdafx.h"

_NT_BEGIN

#include "task.h"
//////////////////////////////////////////////////////////////////////////
//

struct HashWriteTest
{
	HANDLE _hEvent;
	CHashFile* _pHF;
	LONG _iBlocks;
	LONG _iLastBlock;
	LONG _dwThreadCount;

	LONG getBlock()
	{
		LONG i = InterlockedDecrement(&_iBlocks);
		return 0 > i ? i : _iLastBlock - i;
	}

	void StartThread()
	{
		InterlockedIncrementNoFence(&_dwThreadCount);

		if (HANDLE hThread = CreateThread(0, 0, TestThread, this, 0, 0))
		{
			CloseHandle(hThread);
			return;
		}

		EndThread();
	}

	void EndThread()
	{
		if (!InterlockedDecrementNoFence(&_dwThreadCount))
		{
			SetEvent(_hEvent);

			if (_pHF)
			{
				_pHF->Release();
			}
		}
	}

	static ULONG CALLBACK TestThread(void* This)
	{
		static_cast<HashWriteTest*>(This)->DoTest();
		static_cast<HashWriteTest*>(This)->EndThread();
		return GetCurrentThreadId();
	}

	void DoTest()
	{
		CHashFile* pHF = _pHF;
		LONG i, n = 0;
		ULONG cbHash = pHF->GetHashSize();
		while (0 <= (i = getBlock()))
		{
			PUCHAR pbHash;
			if (CHashFile::PCHashBlock pBlock = pHF->GetHashBuffer(i, &pbHash))
			{
				RtlFillMemoryUlong(pbHash, cbHash, i);
				pHF->CompleteHash(pBlock);
				n++;
				//if (!(n & 0xf))Sleep(1);
			}
			else
			{
				break;
			}
		}

		DbgPrint("thread<%08x> = %08x\n", GetCurrentThreadId(), n);
	}

	~HashWriteTest()
	{
		if (_hEvent)
		{
			NtClose(_hEvent);
		}
	}

	HashWriteTest(LONG iBlocks) : _hEvent(0), _pHF(0), _iBlocks(iBlocks), _iLastBlock(iBlocks - 1)
	{
	}

	bool Start(PCWSTR pszFileName, ULONG nThreads, HWND hwnd, ULONG cbHash)
	{
		if (_hEvent = CreateEvent(0, 0, 0, 0))
		{
			if (_pHF = new CHashFile(hwnd))
			{
				if (0 <= _pHF->Create(pszFileName, _iBlocks, cbHash))
				{
					_dwThreadCount = 1;
					do 
					{
						StartThread();

					} while (--nThreads);
					EndThread();
					return true;
				}
				_pHF->Release();
			}
		}

		return false;
	}

	void Wait()
	{
		if (WaitForSingleObject(_hEvent, INFINITE) != WAIT_OBJECT_0)
		{
			__debugbreak();
		}
	}
};

//#define _PRINT_CPP_NAMES_
#include "../inc/asmfunc.h"
bool __scasd(ULONG n, PULONG pu, ULONG v)ASM_FUNCTION;

void ValidateFile(PCWSTR pszFileName, ULONG cbHash)
{
	HANDLE hFile = CreateFileW(pszFileName, FILE_GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, 0);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		FILE_STANDARD_INFORMATION fsi;
		IO_STATUS_BLOCK iosb;
		if (0 <= NtQueryInformationFile(hFile, &iosb, &fsi, sizeof(fsi), FileStandardInformation))
		{
			if (fsi.EndOfFile.QuadPart && !(fsi.EndOfFile.QuadPart % cbHash))
			{
				ULONG N = (ULONG)(fsi.EndOfFile.QuadPart / cbHash);

				ULONG cb = cbHash * min(N, 0x10000), s, v = 0, n;

				if (PUCHAR buf = new UCHAR[cb])
				{
					ULONG dwInHash = cbHash / sizeof(ULONG);

					do 
					{
						s = (ULONG)min(cb, fsi.EndOfFile.QuadPart);

						if (ReadFile(hFile, buf, s, &s, 0))
						{
							n = s / cbHash;
							PULONG pu = (PULONG)buf;

							do 
							{
								if (!__scasd(dwInHash, pu, v))
								{
									__debugbreak();
									break;
								}

							} while (pu += dwInHash, v++, --n);
						}
						else
						{
							__debugbreak();
							break;
						}

					} while (fsi.EndOfFile.QuadPart -= s);

					DbgPrint("-------------\n");

					delete [] buf;
				}
			}
		}
		NtClose(hFile);
	}
}

void TestHashWrite(PCWSTR pszFileName, ULONG nBlocks, ULONG nThreads, ULONG cbHash = 20)
{
	if (HWND hwnd = CreateWindowExW(0, WC_STATIC, 0, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, 0))
	{
		HashWriteTest hrt(nBlocks);

		ULONG t = GetTickCount();

		if (hrt.Start(pszFileName, nThreads, hwnd, cbHash))
		{
			ULONG n = 1, r;
			bool bHashExist = true;
			do 
			{
				MSG msg;
				r = MsgWaitForMultipleObjectsEx(n, &hrt._hEvent, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

				if (r == n)
				{
					while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE))
					{
						DbgPrint("msg(%x %p %p)\n", msg.message, msg.wParam, msg.lParam);
						if (msg.message == WM_DONE && msg.wParam == 0)
						{
							bHashExist = false;
						}
						TranslateMessage(&msg);
						DispatchMessageW(&msg);
					}
					continue;
				}

				if (r < n)
				{
					DbgPrint("\n-=[ read end ]=-\n");
					n = 0;
					continue;
				}
				__debugbreak();

			} while (n || bHashExist);
		}

		DbgPrint("\nTestHashWrite(%S io=%x b=%x) = %u\n", pszFileName, nThreads, nBlocks, GetTickCount() - t);

		DestroyWindow(hwnd);

		ValidateFile(pszFileName, cbHash);
	}
}

void HashData(HANDLE hFile, LONGLONG len, PBYTE pbBuf, ULONG cbBuf, BCRYPT_ALG_HANDLE hAlgorithm, PBYTE pbHash, ULONG cbHash)
{
	IO_STATUS_BLOCK iosb;
	BCRYPT_HASH_HANDLE hHash;

	if (0 <= BCryptCreateHash(hAlgorithm, &hHash, 0, 0, 0, 0, 0))
	{
		ULONG cb;
		do 
		{
			cb = (ULONG)min(len, cbBuf);

			if (0 > NtReadFile(hFile, 0, 0, 0, &iosb, pbBuf, cb, 0, 0) ||
				cb != iosb.Information ||
				0 > BCryptHashData(hHash, pbBuf, cb, 0))
			{
				__debugbreak();
			}

		} while (len -= cb);

		if (0 > BCryptFinishHash(hHash, pbHash, cbHash, 0))
		{
			__debugbreak();
		}

		BCryptDestroyHash(hHash);
	}
}

void TestFileHash(PCWSTR pszFileName, PCWSTR pszHashFile, PCWSTR pszAlgId, ULONGLONG cbBlock)
{
	BCRYPT_ALG_HANDLE hAlgorithm;

	if (0 <= BCryptOpenAlgorithmProvider(&hAlgorithm, pszAlgId, MS_PRIMITIVE_PROVIDER, 0))
	{
		ULONG cbHash, cbResult;

		if (0 <= BCryptGetProperty(hAlgorithm, BCRYPT_HASH_LENGTH, (PUCHAR)&cbHash, sizeof(cbHash), &cbResult, 0))
		{
			PBYTE pbHash1 = (PBYTE)alloca(cbHash*2), pbHash2 = pbHash1 + cbHash;
			IO_STATUS_BLOCK iosb;
			FILE_STANDARD_INFORMATION fsi;
			ULONGLONG nHashes = 0;

			enum { _64kb = 0x10000 };

			if (PUCHAR pbBuf = new UCHAR[_64kb])
			{
				HANDLE hFile;

				hFile = CreateFileW(pszHashFile, FILE_READ_DATA, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, 0);

				if (hFile != INVALID_HANDLE_VALUE)
				{
					if (0 > NtQueryInformationFile(hFile, &iosb, &fsi, sizeof(fsi), FileStandardInformation))
					{
						__debugbreak();
					}

					if (!fsi.EndOfFile.QuadPart || fsi.EndOfFile.QuadPart % cbHash) 
					{
						__debugbreak();
					}

					nHashes = fsi.EndOfFile.QuadPart / cbHash;

					HashData(hFile, fsi.EndOfFile.QuadPart, pbBuf, _64kb, hAlgorithm, pbHash1, cbHash);

					NtClose(hFile);
				}

				hFile = CreateFileW(pszFileName, FILE_READ_DATA, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, 0);

				if (hFile != INVALID_HANDLE_VALUE)
				{
					if (0 > NtQueryInformationFile(hFile, &iosb, &fsi, sizeof(fsi), FileStandardInformation))
					{
						__debugbreak();
					}

					if (((fsi.EndOfFile.QuadPart + cbBlock - 1)/cbBlock) != nHashes) 
					{
						__debugbreak();
					}

					BCRYPT_HASH_HANDLE hHash;

					if (0 <= BCryptCreateHash(hAlgorithm, &hHash, 0, 0, 0, 0, 0))
					{
						ULONGLONG cb;
						do 
						{
							cb = min(cbBlock, (ULONGLONG)fsi.EndOfFile.QuadPart);

							HashData(hFile, cb, pbBuf, _64kb, hAlgorithm, pbHash2, cbHash);

							if (0 > BCryptHashData(hHash, pbHash2, cbHash, 0))
							{
								__debugbreak();
							}

						} while (fsi.EndOfFile.QuadPart -= cb);

						if (0 > BCryptFinishHash(hHash, pbHash2, cbHash, 0))
						{
							__debugbreak();
						}

						BCryptDestroyHash(hHash);

						if (memcmp(pbHash1, pbHash2, cbHash))
						{
							__debugbreak();
						}
						else
						{
							DbgPrint("======== OK =========\n");
						}
					}

					NtClose(hFile);
				}

				delete [] pbBuf;
			}
		}

		BCryptCloseAlgorithmProvider(hAlgorithm, 0);
	}
}

void DoTask(PCWSTR pszFileName, PCWSTR pszHashFile, PCWSTR pszAlgId, ULONGLONG cbBlock, UCHAR nIO)
{
	if (HWND hwnd = CreateWindowExW(0, WC_STATIC, 0, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, 0))
	{
		if (CTask* pTask = new CTask)
		{
			if (0 <= pTask->Create(hwnd, pszFileName, pszHashFile, pszAlgId, cbBlock, FALSE))
			{
				BOOLEAN bPenalty;
				if (0 <= pTask->IsSeekPenalty(bPenalty))
				{
					DbgPrint("bPenalty=%x\n", bPenalty);
				}

				pTask->GetBytesToProcess();

				ULONG t = GetTickCount();

				pTask->Start(nIO);

				//Sleep(6);
				//pTask->Cancel();

				MSG msg;
				while (GetMessageW(&msg, 0, 0, 0))
				{
					DbgPrint("msg(%x %p %p)\n", msg.message, msg.wParam, msg.lParam);
					if (msg.message == WM_DONE && msg.wParam == 0)
					{
						break;
					}
					TranslateMessage(&msg);
					DispatchMessageW(&msg);
				}

				DbgPrint("\ntime=%u\n", GetTickCount() - t);

				if (pTask->GetBytesToProcess())
				{
					__debugbreak();
				}
			}

			pTask->Release();
		}

		DestroyWindow(hwnd);
	}
}

void Test(PCWSTR pszFileName, PCWSTR pszHashFile, PCWSTR pszAlgId, ULONGLONG cbBlock)
{
	TestFileHash(pszFileName, pszHashFile, pszAlgId, cbBlock);
	//TestHashWrite(pszHashFile, (ULONG)cbBlock, 8);
}

_NT_END