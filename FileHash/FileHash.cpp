// FileHash.cpp : Defines the entry point for the console application.
//

#include "pch.h"
#include "resource.h"

_NT_BEGIN
#include "../inc/initterm.h"
#include "task.h"

UCHAR volatile guz = 0;

void Test(PCWSTR pszFileName, PCWSTR pszHashFile, PCWSTR pszAlgId, ULONGLONG cbBlock);

void ShowErrorBox(HWND hwnd, NTSTATUS status, PCWSTR pzCaption)
{
	PWSTR psz;

	if (FormatMessage(
		FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE, 
		GetModuleHandle(L"ntdll"), status, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (PWSTR)&psz, 0, 0))
	{
		MessageBox(hwnd, psz, pzCaption, MB_ICONHAND);
		LocalFree(psz);
	}
	else
	{
		WCHAR sz[32];
		swprintf_s(sz, L"status = %x", status);
		MessageBox(hwnd, sz, pzCaption, MB_ICONHAND);
	}
}

struct DlgData
{
	HANDLE hi[2];
	CTask* _pTask;
	ULONG _n;
	ULONG _time;
	ULONG _shift;
	BOOL _bTimerActive;

	enum { TimerId = 1 };

	DlgData()
	{
	}

	~DlgData()
	{
	}

	static INT_PTR CALLBACK _DlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		DlgData* This;

		if (uMsg == WM_INITDIALOG)
		{
			This = (DlgData*)lParam;
			SetWindowLongPtrW(hwnd, DWLP_USER, lParam);
		}
		else
		{
			This = (DlgData*)GetWindowLongPtrW(hwnd, DWLP_USER);
		}

		return This->DlgProc(hwnd, uMsg, wParam, lParam);
	}

	INT_PTR DlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		case WM_DONE:
			if (_bTimerActive)
			{
				KillTimer(hwnd, TimerId);
				_bTimerActive = FALSE;
			}
			OnTimer(hwnd);
			if (_pTask)
			{
				_pTask->Release();
				_pTask = 0;
			}
			if (lParam < 0)
			{
				ShowErrorBox(hwnd, (NTSTATUS)lParam, L"calc hash fail !");
			}
			else
			{
				MessageBoxW(hwnd, L"Done !", L"", MB_OK|MB_ICONINFORMATION);
			}
			EnableButtons(hwnd, TRUE);
			break;
		case WM_TIMER:
			OnTimer(hwnd);
			break;
		case WM_COMMAND:
			switch (wParam)
			{
			case IDOK: 
				if (!_pTask)
				{
					OnOk(hwnd);
				}
				break;
			case IDCANCEL:
			case IDABORT:
				if (_pTask)
				{
					_pTask->Rundown();
					break;
				}
				EndDialog(hwnd, 0);
				break;
			}
			break;

		case WM_NCDESTROY:
			if (_pTask)//never must be
			{
				_pTask->Release();
			}
			break;

		case WM_DESTROY:
			if (_bTimerActive) KillTimer(hwnd, TimerId);
			if (hi[1]) DestroyIcon((HICON)hi[1]);
			if (hi[0]) DestroyIcon((HICON)hi[0]);
			break;

		case WM_INITDIALOG:
			InitUserData(hwnd);
			break;
		}

		return 0;
	}

	void OnTimer(HWND hwnd)
	{
		if (_pTask)
		{
			ULONGLONG dwBytes = _pTask->GetBytesToProcess();
			SendMessage(GetDlgItem(hwnd, IDC_PROGRESS1), PBM_SETPOS, 
				(ULONG)(dwBytes >> _shift), 0);

			WCHAR sz[128];
			swprintf_s(sz, L"bytes left: %9I64u %8u ms", dwBytes, GetTickCount() - _time);
			SetDlgItemTextW(hwnd, IDC_STATIC1, sz);
		}
	}

	void InitUserData(HWND hwnd)
	{
		_pTask = 0;

		HANDLE hIcon;
		if (hIcon = LoadImageW((HINSTANCE)&__ImageBase, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0))
		{
			SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)(hi[0] = hIcon));
		}
		
		if (hIcon = LoadImageW((HINSTANCE)&__ImageBase, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0))
		{
			SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)(hi[1] = hIcon));
		}

		HWND hwndCB;
		ULONG n;
		int i;

		if (hwndCB = GetDlgItem(hwnd, IDC_COMBO2))
		{
			static const PCWSTR s_AlgId[] = {
				BCRYPT_SHA512_ALGORITHM, BCRYPT_SHA256_ALGORITHM, BCRYPT_SHA1_ALGORITHM, BCRYPT_MD5_ALGORITHM 
			};

			n = _countof(s_AlgId);

			do 
			{
				PCWSTR AlgId = s_AlgId[--n];

				if (0 <= (i = ComboBox_AddString(hwndCB, AlgId)))
				{
					ComboBox_SetItemData(hwndCB, i, AlgId);
				}
			} while (n);

			ComboBox_SetCurSel(hwndCB, 0);
		}

		if (hwndCB = GetDlgItem(hwnd, IDC_COMBO1))
		{
			static const PCWSTR s_granularity[] = {
				L"KB", L"MB", L"GB"
			};

			static const ULONG s_shift[] = {
				10, 20, 30
			};

			n = _countof(s_granularity);

			do 
			{
				PCWSTR granularity = s_granularity[--n];
				if (0 <= (i = ComboBox_AddString(hwndCB, granularity)))
				{
					ComboBox_SetItemData(hwndCB, i, s_shift[n]);
				}
			} while (n);

			ComboBox_SetCurSel(hwndCB, 1);
		}

		SetDlgItemTextW(hwnd, IDC_EDIT3, L"1");
		SetDlgItemTextW(hwnd, IDC_EDIT4, L"8");

		_bTimerActive = FALSE;
	}

	void OnOk(HWND hwnd)
	{
		PWSTR pszFile[2], psz;
		const static UINT id[] = { IDC_EDIT1, IDC_EDIT2 };

		ULONG n = _countof(id), len;

		HWND hwndCtl;

		do 
		{
			hwndCtl = GetDlgItem(hwnd, id[--n]);

			if (!(len = GetWindowTextLengthW(hwndCtl)))
			{
				SetFocus(hwndCtl);
				return ;
			}

			pszFile[n] = psz = (PWSTR)alloca(++len*sizeof(WCHAR));

			GetWindowTextW(hwndCtl, psz, len);

		} while (n);

		BOOL bTranslated;
		ULONGLONG cbBlock = GetDlgItemInt(hwnd, IDC_EDIT3, &bTranslated, FALSE);

		if (!bTranslated)
		{
			SetFocus(GetDlgItem(hwnd, IDC_EDIT3));
			return;
		}

		int i = ComboBox_GetCurSel(hwndCtl = GetDlgItem(hwnd, IDC_COMBO1));

		if (0 > i)
		{
			SetFocus(GetDlgItem(hwnd, IDC_COMBO1));
			return;
		}

		cbBlock <<= ComboBox_GetItemData(hwndCtl, i);

		if (!cbBlock)
		{
			cbBlock = MAXLONGLONG;
		}

		UINT nIO = GetDlgItemInt(hwnd, IDC_EDIT4, &bTranslated, FALSE);

		if (!bTranslated || nIO - 1 > 98)
		{
			SetFocus(GetDlgItem(hwnd, IDC_EDIT4));
			return;
		}

		if (0 > (i = ComboBox_GetCurSel(hwndCtl = GetDlgItem(hwnd, IDC_COMBO2))))
		{
			SetFocus(GetDlgItem(hwnd, IDC_COMBO2));
			return;
		}

		if (CTask* pTask = new CTask)
		{
			NTSTATUS status = pTask->Create(hwnd, pszFile[0], pszFile[1], 
				(PCWSTR)ComboBox_GetItemData(hwndCtl, i), cbBlock, 
				IsDlgButtonChecked(hwnd, IDC_CHECK1) == BST_CHECKED);

			if (0 <= status)
			{
				if (1 < nIO)
				{
					BOOLEAN bPenalty;
					if (0 <= pTask->IsSeekPenalty(bPenalty) && bPenalty)
					{
						if (MessageBoxW(hwnd, L"use single I/O ?", L"Disk have seek penalty !", MB_ICONQUESTION|MB_YESNO) == IDYES)
						{
							nIO = 1;
						}
					}
				}

				ULONGLONG dwBytes = pTask->GetBytesToProcess();

				_shift = 0;

				while (dwBytes > MAXLONG)
				{
					dwBytes >>= 1, _shift++;
				}

				hwndCtl = GetDlgItem(hwnd, IDC_PROGRESS1);

				SendMessage(hwndCtl, PBM_SETRANGE32, 0, (ULONG)dwBytes);
				SendMessage(hwndCtl, PBM_SETPOS, (ULONG)dwBytes, 0);

				_pTask = pTask;
				EnableButtons(hwnd, FALSE);

				_bTimerActive = SetTimer(hwnd, TimerId, 100, 0) != 0;
				_time = GetTickCount();
				pTask->Start((UCHAR)nIO);

				return ;
			}

			pTask->Release();

			ShowErrorBox(hwnd, status, 0);
		}
	}

	void EnableButtons(HWND hwnd, BOOL b)
	{
		EnableWindow(GetDlgItem(hwnd, IDOK), b);
		EnableWindow(GetDlgItem(hwnd, IDABORT), !b);
	}
};

void CALLBACK ep(void*)
{
	BOOLEAN b;
	RtlAdjustPrivilege(SE_MANAGE_VOLUME_PRIVILEGE, TRUE, FALSE, &b);

	DlgData dd;
	DialogBoxParamW((HINSTANCE)&__ImageBase, MAKEINTRESOURCEW(IDD_DIALOG1), HWND_DESKTOP, DlgData::_DlgProc, (LPARAM)&dd);
	
	ExitProcess(0);
}

_NT_END


