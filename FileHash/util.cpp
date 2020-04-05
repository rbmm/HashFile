#include "pch.h"

_NT_BEGIN

#include "util.h"

extern volatile UCHAR guz;

ULONG gcd(ULONG m, ULONG n)
{
	for (;;)
	{
		if (m == n)
		{
			return m;
		}

		if (m == 1 || n == 1) return 1;

		if (m & 1)
		{
			if (n & 1)
			{
				if (m < n)
				{
					n = (n - m) >> 1;
					continue;
				}
				m = (m - n) >> 1;
				continue;
			}
			n >>= 1;
			continue;
		}

		m >>= 1;

		if (n & 1)
		{
			continue;
		}

		return gcd(m, n >> 1) << 1;
	}
}

ULONG lcm(ULONG m, ULONG n)
{
	return m * n / gcd(m, n);
}

NTSTATUS GetSeekPenalty(HANDLE hFile, BOOLEAN& SeekPenalty)
{
	NTSTATUS status;
	IO_STATUS_BLOCK iosb;

	union {
		PVOID buf;
		PWSTR psz;
		POBJECT_NAME_INFORMATION poni;
		PFILE_VOLUME_NAME_INFORMATION pfvni;
	};

	ULONG cb = 0, rcb = 0x40;
	PVOID stack = alloca(guz);
	bool bFileVolumeNameInformation = true;

__loop:
	if (cb < rcb)
	{
		cb = RtlPointerToOffset(buf = alloca(rcb - cb), stack);
	}

	status = bFileVolumeNameInformation
		? NtQueryInformationFile(hFile, &iosb, buf, cb, FileVolumeNameInformation)
		: NtQueryObject(hFile, ObjectNameInformation, buf, cb, &rcb);

	switch (status)
	{
	case STATUS_BUFFER_OVERFLOW:
		if (bFileVolumeNameInformation)
		{
			rcb = pfvni->DeviceNameLength + FIELD_OFFSET(FILE_VOLUME_NAME_INFORMATION, DeviceName);
			goto __loop;
		}
	case STATUS_BUFFER_TOO_SMALL:
	case STATUS_INFO_LENGTH_MISMATCH:
		goto __loop;
	}

	if (0 > status && bFileVolumeNameInformation)
	{
		bFileVolumeNameInformation = false;
		goto __loop;
	}

	if (0 <= status)
	{
		if (bFileVolumeNameInformation)
		{
			OBJECT_NAME_INFORMATION oni;
			oni.Name.Buffer = pfvni->DeviceName;
			oni.Name.MaximumLength = oni.Name.Length = (USHORT)pfvni->DeviceNameLength;
			poni = &oni;
			goto __FileVolumeName;
		}

		DbgPrint("%wZ\n", &poni->Name);

		PFILE_NAME_INFORMATION pfni = (PFILE_NAME_INFORMATION)alloca(rcb);

		if (0 <= (status = NtQueryInformationFile(hFile, &iosb, pfni, rcb, FileNameInformation)))
		{
			status = STATUS_UNSUCCESSFUL;

			if (pfni->FileNameLength < poni->Name.Length)
			{
				if (!memcmp(pfni->FileName,
					RtlOffsetToPointer(poni->Name.Buffer, poni->Name.Length -= (USHORT)pfni->FileNameLength),
					pfni->FileNameLength))
				{
				__FileVolumeName:
					OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &poni->Name };

					DbgPrint("%wZ\n", &poni->Name);

					if (0 <= (status = NtOpenFile(&oa.RootDirectory, SYNCHRONIZE, &oa,
						&iosb, FILE_SHARE_VALID_FLAGS, FILE_SYNCHRONOUS_IO_NONALERT)))
					{
						STORAGE_DEVICE_NUMBER sdn;

						status = NtDeviceIoControlFile(oa.RootDirectory, 0, 0, 0, &iosb,
							IOCTL_STORAGE_GET_DEVICE_NUMBER, 0, 0, &sdn, sizeof(sdn));

						NtClose(oa.RootDirectory), oa.RootDirectory = 0;

						if (0 <= status)
						{
							WCHAR Harddisk[64];
							// L"\\Device\\Harddisk%d\\Partition0"
							swprintf_s(Harddisk, L"\\GLOBAL??\\PhysicalDrive%d", sdn.DeviceNumber);

							RtlInitUnicodeString(&poni->Name, Harddisk);

							if (0 <= (status = NtOpenFile(&oa.RootDirectory,
								SYNCHRONIZE | FILE_READ_DATA | FILE_WRITE_DATA, &oa,
								&iosb, FILE_SHARE_VALID_FLAGS, FILE_SYNCHRONOUS_IO_NONALERT)))
							{
								DEVICE_SEEK_PENALTY_DESCRIPTOR dspd;
								static const STORAGE_PROPERTY_QUERY spq = { StorageDeviceSeekPenaltyProperty, PropertyStandardQuery };

								status = NtDeviceIoControlFile(oa.RootDirectory, 0, 0, 0, &iosb, IOCTL_STORAGE_QUERY_PROPERTY,
									const_cast<STORAGE_PROPERTY_QUERY*>(&spq), sizeof(spq), &dspd, sizeof(dspd));

								SeekPenalty = dspd.IncursSeekPenalty;

								NtClose(oa.RootDirectory);
							}
						}
					}
				}
			}
		}
	}

	return status;
}

NTSTATUS GetSectorSizeAndAligment(HANDLE hFile, ULONG& AlignmentRequirement, ULONG& SectorSize, PLARGE_INTEGER EndOfFile)
{
	IO_STATUS_BLOCK iosb;
	FILE_ALIGNMENT_INFORMATION fai;

	NTSTATUS status = NtQueryInformationFile(hFile, &iosb, &fai, sizeof(fai), FileAlignmentInformation);

	if (0 <= status)
	{
		if (fai.AlignmentRequirement & (fai.AlignmentRequirement + 1) ||
			fai.AlignmentRequirement > FILE_512_BYTE_ALIGNMENT)
		{
			status = STATUS_INVALID_DEVICE_OBJECT_PARAMETER;
		}
		else
		{
			FILE_FS_SIZE_INFORMATION ffsi;

			if (0 <= (status = NtQueryVolumeInformationFile(hFile, &iosb, &ffsi, sizeof(ffsi), FileFsSizeInformation)))
			{
				if (ffsi.BytesPerSector % (fai.AlignmentRequirement + 1))
				{
					status = STATUS_INVALID_DEVICE_OBJECT_PARAMETER;
				}
				else
				{
					if (EndOfFile)
					{
						FILE_STANDARD_INFORMATION fsi;

						status = NtQueryInformationFile(hFile, &iosb, &fsi, sizeof(fsi), FileStandardInformation);

						if (0 > status)
						{
							return status;
						}

						EndOfFile->QuadPart = fsi.EndOfFile.QuadPart;
					}

					AlignmentRequirement = fai.AlignmentRequirement;
					SectorSize = ffsi.BytesPerSector;

					return STATUS_SUCCESS;
				}
			}
		}
	}

	return status;
}

_NT_END