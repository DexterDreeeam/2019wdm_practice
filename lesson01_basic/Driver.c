
/*
* GDT - global descriptor table
* IDT - interrupt descriptor table
* CPL - current privilege level
* DPL - descriptor privilege level
* RPL - requested privilege level
* Ring 0,1,2,3
* Eflags registor
*
*
* prefix notify:
* Cc - cache manager
* Cm - configuration manager
* Dbg/Kd - windows debug
* Ex - exception
* FsRtl - runtime library for file system
* Hal - hardware abstraction layer
* Io - io manager
* Ke - kernel layer
* Mm - memory manager
* Ob - object manager
* Ps - process & thread
* Rtl - runtime library
* Po - power manager
* Pp - plug & play manager
* Nt - new technology(windows kernel functions)
* Zw - N/A(usermode context to kernelmode)
* Halp - prevent functions for Hal
* Iop - prevent functions for Io
* Ki - interval functions for kernel layer
* Mi - interval functions for memory manager
*/

/*
 *Logical Address
 *       |   <- GDT
 * Linear Address
 *       |   <- Page Manager
 *Physical Address
 *
 *
 * IRQL - interrupt request level
        - PASSIVE_LEVEL  - 0
        - APC_LEVEL      - 1
        - DISPATCH_LEVEL - 2
        - DIRQL_LEVEL    - 3 ~ 31
		e.g. Thread Sceduling is DISPATCH_LEVEL
 */

#include "ntddk.h" //core Windows Kernel definitions for drivers
#include "ntifs.h"
//#include <wdf.h> //definitions for drivers based on WDF

KTIMER myTimer;
KDPC myDpc; //deferred process call
LARGE_INTEGER due = { 0 };

VOID Unload(IN PDRIVER_OBJECT DriverObject)
{
	KeCancelTimer(&myTimer);
	DbgPrint("Driver Unload.\r\n");
}

NTSTATUS DriverEntry(
	IN PDRIVER_OBJECT DriverObject,
	IN PUNICODE_STRING RegistryPath
)
{
	NTSTATUS status = STATUS_SUCCESS;

	/*********************************************************************************************/
	///string manipulation
	//UNICODE_STRING string = RTL_CONSTANT_STRING(L"Hello Driver.\r\n");

	//UNICODE_STRING string = { 0 };
	//RtlInitUnicodeString(&string, L"Hello Driver.\r\n");

	UNICODE_STRING string = { 0 };
	WCHAR strbuf[120] = { 0 };
	string.Buffer = strbuf;
	string.Length = string.MaximumLength = wcslen(L"Hello Driver.\r\n") * sizeof(WCHAR);
	wcscpy(string.Buffer, L"Hello Driver.\r\n");
	DbgPrint("%wZ", &string);

	/*********************************************************************************************/
	///memory allocate
	#define MyTag 'abcd'
	ULONG length = 100;
	PWCHAR addr = (PWCHAR)ExAllocatePoolWithTag(NonPagedPool, length, MyTag);
	#undef MyTag
	if (NULL != addr)
	{
		ExFreePool(addr);
	}

	/*********************************************************************************************/
	///system timer
	LARGE_INTEGER system_time = { 0 };
	LARGE_INTEGER local_time = { 0 };
	TIME_FIELDS local_time_fields = { 0 };
	ExSystemTimeToLocalTime(&system_time, &local_time);
	RtlTimeToTimeFields(&local_time, &local_time_fields);
	DbgPrint("Time is %4d-%2d-%2d %2d-%2d-%2d.\r\n",
		local_time_fields.Year,
		local_time_fields.Month,
		local_time_fields.Day,
		local_time_fields.Hour,
		local_time_fields.Minute,
		local_time_fields.Second
	);

	#define DELAY_ONE_MICROSEC (-10)
	#define DELAY_ONE_MILLISEC (DELAY_ONE_MICROSEC * 1000)
	LARGE_INTEGER interval = { 0 };
	int myTime = 5 * 1000;
	interval.QuadPart = myTime * DELAY_ONE_MILLISEC;
	KeDelayExecutionThread(KernelMode, FALSE, &interval);
	DbgPrint("Delay Loading.\r\n");

	/*********************************************************************************************/
	///set delay timer
	VOID myDpcFunc(
		IN PKDPC Dpc,
		IN PVOID context,
		IN PVOID SysArgument1,
		IN PVOID SysArgument2
	);
	due.QuadPart = 5000 * DELAY_ONE_MILLISEC;
	KeInitializeTimer(&myTimer);
	KeInitializeDpc(&myDpc, myDpcFunc, NULL);
	KeSetTimer(&myTimer, due, &myDpc);

	/*********************************************************************************************/
	///system thread
	HANDLE ThreadHandle = NULL;
	VOID MySysThreadFunc(IN PVOID Context);
	status = PsCreateSystemThread(
		&ThreadHandle,
		0,
		NULL,
		NULL,
		NULL,
		MySysThreadFunc,
		NULL
	);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("Create thread failed.\r\n");
		return status;
	}
	ZwClose(ThreadHandle);

	/*********************************************************************************************/
	///file handle
	OBJECT_ATTRIBUTES obj_attributeW, obj_attributeR;
	UNICODE_STRING filenameW = RTL_CONSTANT_STRING(L"\\??\\c:\\1.txt");
	UNICODE_STRING filenameR = RTL_CONSTANT_STRING(L"\\??\\c:\\2.txt");
	HANDLE fileHandleW = NULL, fileHandleR = NULL;
	IO_STATUS_BLOCK IoStatusBlockW = { 0 }, IoStatusBlockR = { 0 };
	InitializeObjectAttributes(
		&obj_attributeW,
		&filenameW,
		OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
		NULL,
		NULL
	);
	status = ZwCreateFile(
		&fileHandleW,
		GENERIC_READ | GENERIC_WRITE,
		&obj_attributeW,
		&IoStatusBlockW,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ,
		FILE_OPEN_IF,
		FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
		NULL,
		0
	);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("Create file failed.\r\n");
	}
	InitializeObjectAttributes(
		&obj_attributeR,
		&filenameR,
		OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
		NULL,
		NULL
	);
	status = ZwCreateFile(
		&fileHandleR,
		GENERIC_READ,
		&obj_attributeR,
		&IoStatusBlockR,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ,
		FILE_OPEN, //file exist
		FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
		NULL,
		0
	);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("Open file failed.\r\n");
	}
	NTSTATUS copyFile(HANDLE fileR, HANDLE fileW);
	status = copyFile(fileHandleR, fileHandleW);
	if (NULL != fileHandleW)
	{
		ZwClose(fileHandleW);
	}
	if (NULL != fileHandleR)
	{
		ZwClose(fileHandleR);
	}

	/*********************************************************************************************/
	///query file information
	FILE_STANDARD_INFORMATION fileInfo = { 0 };
	status = ZwQueryInformationFile(
		fileHandleR,
		&IoStatusBlockR,
		&fileInfo,
		sizeof(FILE_STANDARD_INFORMATION),
		FileStandardInformation
	);
	if (NT_SUCCESS(status))
	{
		DbgPrint("File size is %d.\r\n", fileInfo.EndOfFile.QuadPart);
	}

	/*********************************************************************************************/
	///delete file
	status = ZwDeleteFile(&obj_attributeR);
	if (NT_SUCCESS(status))
	{
		DbgPrint("Delete file.\r\n");
	}

	/*********************************************************************************************/
	DriverObject->DriverUnload = Unload;
	return status; 
}

VOID myDpcFunc(
	IN PKDPC Dpc,
	IN PVOID context,
	IN PVOID SysArgument1,
	IN PVOID SysArgument2
	)
{
	KeSetTimer(&myTimer, due, &myDpc);
	DbgPrint("Timer is working.\r\n");
}

VOID MySysThreadFunc(
	IN PVOID Context
	)
{
	DbgPrint("My new thread.\r\n");
	PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS copyFile(HANDLE fileR, HANDLE fileW)
{
	NTSTATUS status = STATUS_SUCCESS;
	IO_STATUS_BLOCK ioStatusBlock = { 0 };
	PVOID buffer = NULL;
	ULONG length = 4096;
	LARGE_INTEGER offset = { 0 };
	#define TAG 'file'
	buffer = ExAllocatePoolWithTag(NonPagedPool, length, TAG);
	#undef TAG
	while (1)
	{
		length = 4096;
		status = ZwReadFile(
			fileR,
			NULL, NULL, NULL,
			&ioStatusBlock,
			buffer, length, &offset,
			NULL
		);
		if (!NT_SUCCESS(status))
		{
			if (status == STATUS_END_OF_FILE) break;
			else goto LABEL_ERROR;
		}
		length = ioStatusBlock.Information; //real length
		status = ZwWriteFile(
			fileR,
			NULL, NULL, NULL,
			&ioStatusBlock,
			buffer, length, &offset,
			NULL
		);
		if (!NT_SUCCESS(status)) goto LABEL_ERROR;
		offset.QuadPart += length;
	}
	ExFreePool(buffer);
	return STATUS_SUCCESS;



LABEL_ERROR:
	if (buffer != NULL)
		ExFreePool(buffer);
	return STATUS_UNSUCCESSFUL;
}
