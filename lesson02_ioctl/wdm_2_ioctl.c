

#include "ntddk.h"
#include "intrin.h"

#define DELAY_ONE_MICROSECOND (-10)
#define DELAY_ONE_MILLISECOND (DELAY_ONE_MICROSECOND * 1000)
#define THREAD_NUM 64

#define DEVICE_SEND \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA)
#define DEVICE_REC \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA)

UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\myDevice_abc");
UNICODE_STRING SymLinkName = RTL_CONSTANT_STRING(L"\\??\\myDeivceLink_abc");
PDEVICE_OBJECT DeviceObject = NULL;

KSEMAPHORE Sema = 0;
KSPIN_LOCK datalock;

ULONG datanum = 0;
ULONG cpunum = 0;
HANDLE thread[THREAD_NUM] = { 0 };
PVOID thread_obj[THREAD_NUM] = { 0 };
BOOLEAN signal = FALSE;
PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info = NULL;
UINT64 struct_size = 0;
PVOID outputBuffer = NULL;

typedef struct
{
    ULONG cpuid;
    ULONG cputempdata; //cpu temperature
}TEMP_NODE, *PTEMP_NODE;

VOID Unload(IN PDRIVER_OBJECT DriverObject)
{
    signal = TRUE;

    if (NULL != Info)
    {
        ExFreePool(Info);
    }

    KeReleaseSemaphore(&Sema, 0, cpunum, FALSE);

    for (int i = 0; i != cpunum && thread_obj[i]; ++i)
    {
        KeWaitForSingleObject(
            thread_obj[i], 
            Executive, 
            KernelMode, 
            FALSE, 
            NULL
            );
        ObDereferenceObject(thread_obj[i]);
    }

    IoDeleteSymbolicLink(&SymLinkName);
    IoDeleteDevice(DeviceObject);
    DbgPrint("Driver unload.\r\n");
}

NTSTATUS DispatchPassThru(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
    )
{
    PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_SUCCESS;

    switch (irpsp->MajorFunction)
    {
    case IRP_MJ_CREATE:
        KdPrint(("Create request.\r\n"));
        break;
    case IRP_MJ_CLOSE:
        KdPrint(("Close request.\r\n"));
        break;
    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}

NTSTATUS DispatchDevCtl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
    )
{
    PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_SUCCESS;

    PVOID buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG inLength = irpsp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLength = irpsp->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG returnLength;
    WCHAR * demo = L"Sample returned from driver.";
    LARGE_INTEGER interval = { 0 };

    interval.QuadPart = DELAY_ONE_MILLISECOND * 2;

    switch (irpsp->Parameters.DeviceIoControl.IoControlCode)
    {
    case DEVICE_SEND: //send data from app to kernel driver
        if (!strncmp(buffer, "start", 6)) 
        {
            KdPrint(("Start retrieve cpu temperature.\r\n"));
            create_systhread();
        }
        break;
    case DEVICE_REC: //receivce data from kernel driver to app
        outputBuffer = buffer;
        datanum = 0;
        KeReleaseSemaphore(&Sema, 0, cpunum, FALSE); //Sema += cpu num(4)
        while (1)
        {
            KeDelayExecutionThread(KernelMode, FALSE, &interval);
            if (datanum == cpunum) //all threads done
            {
                returnLength = sizeof(TEMP_NODE) * cpunum;
                break;
            }
        }
        break;
    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    Irp->IoStatus.Information = returnLength;
    Irp->IoStatus.Status = status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}

VOID MyProc(PVOID context)
{
    GROUP_AFFINITY groupaff = { 0 };
    ULONG index = 0x19c;
    UINT64 readout = 0;
    ULONG delta = 0;
    ULONG cputemp = 0; //cpu temperature
    LARGE_INTEGER interval = { 0 };
    ULONG cpuid = ((UINT64)context - (UINT64)Info) / struct_size;
    KIRQL oirql; //interrupt request level
    PTEMP_NODE tnode;

    interval.QuadPart = DELAY_ONE_MILLISECOND * 3000;
    groupaff = ((PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)context)->Processor.GroupMask[0];
    
    KeSetSystemGroupAffinityThread(&groupaff, NULL);

    while (1)
    {
        KeWaitForSingleObject(&Sema, Executive, KernelMode, 0, NULL); //wait if Sema <= 0

        if (signal == TRUE)
            break;
        
        //KeDelayExecutionThread(KernelMode, FALSE, &interval);
        
        readout = __readmsr(index); //model specific register

    retry:
        if ((readout & 0x80000000))
        {
            delta = (readout >> 16) & 0x7f;
            cputemp = 100 - delta;
            DbgPrint("Cpu %d temperature is %d.\r\n", cpuid, cputemp);
            
            tnode = (PTEMP_NODE)outputBuffer;
            tnode[cpuid].cputempdata = cputemp;
            tnode[cpuid].cpuid = cpuid;

            KeAcquireSpinLock(&datalock, &oirql);
            ++datanum;
            KeReleaseSpinLock(&datalock, oirql);
        }
        else
        {
            goto retry;
        }
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

VOID create_systhread()
{
    NTSTATUS status;

    for (int i = 0; i != cpunum; ++i)
    {
        status = PsCreateSystemThread(
            &thread[i],
            0,
            NULL, NULL, NULL,
            MyProc,
            (PVOID)((UINT64)Info + struct_size/**/)
            );
        if (!NT_SUCCESS(status))
        {
            DbgPrint("Creating thread failed.\r\n");
            break;
        }

        status = ObReferenceObjectByHandle(
            thread[i],
            THREAD_ALL_ACCESS,
            NULL,
            KernelMode,
            &thread_obj[i],
            NULL/*?*/
            );
        ZwClose(thread[i]);
        if (!NT_SUCCESS(status))
        {
            break;
        }
    }
}

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;
    int cpuinfo[4] = { 0 };
    ULONG size = 0;

    DriverObject->DriverUnload = Unload;

    __cpuid(cpuinfo, 0);
    if (cpuinfo[1] != 0x756e6547 || cpuinfo[2] != 0x6c65746e || cpuinfo[3] != 0x49656e69)
    {
        DbgPrint("Not intel CPU.\r\n");
        return STATUS_UNSUCCESSFUL;
    }

    __cpuid(cpuinfo, 1);
    if ((cpuinfo[3] & 0x20) == 0)
    {
        DbgPrint("Not support rdmsr.\r\n");
        return STATUS_UNSUCCESSFUL;
    }

    __cpuid(cpuinfo, 6);
    if ((cpuinfo[0] & 1) == 0)
    {
        DbgPrint("Not support digital thermal sensor.\r\n");
        return STATUS_UNSUCCESSFUL;
    }

    status = KeQueryLogicalProcessorRelationship(NULL, RelationProcessorCore, NULL, &size);

    if (status == STATUS_INFO_LENGTH_MISMATCH)
    {
        Info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)ExAllocatePoolWithTag(PagedPool, size, 'abcd');
        if (Info == NULL)
        {
            return STATUS_UNSUCCESSFUL;
        }
        RtlZeroMemory(Info, size);
        status = KeQueryLogicalProcessorRelationship(NULL, RelationProcessorCore, Info, &size);
        if (!NT_SUCCESS(status))
        {
            ExFreePool(Info);
            return STATUS_UNSUCCESSFUL;
        }

        struct_size = (UINT64)(&((PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)0)->Processor) /*+ sizeof(PROCESS)*/;
        cpunum = size / struct_size;
    }
    else
    {
        return STATUS_UNSUCCESSFUL;
    }
    
    KeInitializeSemaphore(&Sema, 0, cpunum);
    KeInitializeSpinLock(&datalock);

    status = IoCreateDevice(
        DriverObject,
        0,
        &DeviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &DeviceObject
        );
    if (!NT_SUCCESS(status))
    {
        KdPrint(("Create device failed.\r\n"));
        return status;
    }

    status = IoCreateSymbolicLink(&SymLinkName, &DeviceName);
    if (!NT_SUCCESS(status))
    {
        KdPrint(("Create symbolic link failed.\r\n"));
        IoDeleteDevice(DeviceObject);
        return status;
    }

    for (int i = 0; i != IRP_MJ_MAXIMUM_FUNCTION; ++i)
    {
        DriverObject->MajorFunction[i] = DispatchPassThru;
    }
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDevCtl;

    return STATUS_SUCCESS;
}