#pragma once

#define NTDLL__(type)	extern "C" __declspec(dllimport) type 
#define NTDLL_(type)	NTDLL__(type) CALLBACK
#define NTDLL			NTDLL_(NTSTATUS)
#define NTDLL_V NTDLL_(void)

EXTERN_C_START

typedef struct _RTLP_CURDIR_REF
{
	LONG ReferenceCount;
	HANDLE DirectoryHandle;
} RTLP_CURDIR_REF, *PRTLP_CURDIR_REF;

typedef struct _RTL_RELATIVE_NAME_U
{
	UNICODE_STRING RelativeName;
	HANDLE ContainingDirectory;
	PRTLP_CURDIR_REF CurDirRef;
} RTL_RELATIVE_NAME_U, *PRTL_RELATIVE_NAME_U;

typedef VOID (NTAPI * APC_CALLBACK_FUNCTION)(
	NTSTATUS status,
	ULONG_PTR Information,
	PVOID Context
	);

extern IMAGE_DOS_HEADER __ImageBase;

NTSYSAPI
NTSTATUS
NTAPI
RtlDosPathNameToNtPathName_U_WithStatus(
										_In_ PCWSTR DosFileName,
										_Out_ PUNICODE_STRING NtFileName,
										_Out_opt_ PCWSTR *FilePart,
										_Out_opt_ PRTL_RELATIVE_NAME_U RelativeName
										);

NTSYSAPI
NTSTATUS
NTAPI 
RtlSetIoCompletionCallback(
						   _In_ HANDLE FileHandle, 
						   _In_ APC_CALLBACK_FUNCTION Function, 
						   _In_ ULONG Flags
						   );

NTSYSAPI
NTSTATUS
NTAPI
RtlAdjustPrivilege(
				   _In_ ULONG Privilege,
				   _In_ BOOLEAN Enable,
				   _In_ BOOLEAN Client,
				   _Out_ PBOOLEAN WasEnabled
				   );

NTSYSAPI NTSTATUS NTAPI RtlGetLastNtStatus(  );


EXTERN_C_END
