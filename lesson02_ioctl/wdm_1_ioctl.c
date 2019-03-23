
/*
 *
 *  [application]
 *   \         |                         (user mode)
 * ------------------------------------------------
 *    \        |                       (kernel mode)
 * [Kernel Service] - Nt_xxxx
 *     \       |
 *    [Io manager]  - IRP
 *       \     |
 *       [drivers]
 *         \   |
 *       [hardware]
 *
 *
 * IRP - Io Request Packet
 * DriverObject - note the driver
 * DeviceObject - note the device
 *
 *
 *  [Handle]
 *   \    |
 *   [ IRP ]
 *    \   |
 *  [driverObj] (stack)
 *     \  |
 *  [deviceObj]
 *
 *
 *
 */


#include "ntddk.h"

UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\myDevice_abc");
UNICODE_STRING SymLinkName = RTL_CONSTANT_STRING(L"\\??\\myDeviceSymLink_abc");
PDEVICE_OBJECT DeviceObject = NULL;

VOID Unload(PDRIVER_OBJECT DriverObject)
{
    IoDeleteSymbolicLink(&SymLinkName);
    IoDeleteDevice(DeviceObject);
    KdPrint(("Driver unload.\r\n"));
}

NTSTATUS DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath
)
{
    DriverObject->DriverUnload = Unload;
    NTSTATUS status;

    //**********************************************
    /// create device
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

    //**********************************************
    /// create symbolic link for user mode app
    /// user application can get the handle with symbolic link
    status = IoCreateSymbolicLink(
        &SymLinkName, &DeviceName
    );
    if (!NT_SUCCESS(status))
    {
        KdPrint(("Create symbolic link failed.\r\n"));
        IoDeleteDevice(DeviceObject);
        return status;
    }

    //**********************************************
    /// deal with IRP
    NTSTATUS DispatchCustomFunc(PDEVICE_OBJECT DeviceObject, PIRP Irp);
    NTSTATUS DispatchDevCtl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
    for (int i = 0; i != IRP_MJ_MAXIMUM_FUNCTION; ++i)
    {
        //general dispatch function
        DriverObject->MajorFunction[i] = DispatchCustomFunc;
    }
    //DriverObject->MajorFunction[IRP_MJ_READ] = 
    //    DispatchCustom_Read;
    //DriverObject->MajorFunction[IRP_MJ_WRITE] = 
    //    DispatchCustom_WRITE;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] =
        DispatchDevCtl; //device control dispatch function

    //**********************************************

    return status;
}

NTSTATUS DispatchCustomFunc(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
    )
{
    PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_SUCCESS;

    switch (irpsp->MajorFunction)
    {
    case IRP_MJ_CREATE: //create device IRP
        KdPrint(("Create request.\r\n"));
        break;
    case IRP_MJ_CLOSE: //close device IRP
        KdPrint(("Close request.\r\n"));
        break;
    default:
        status = STATUS_INVALID_PARAMETER; // don't support
        break;
    }

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}

#define DEVICE_SEND \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA)
#define DEVICE_REC \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA)

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

    switch (irpsp->Parameters.DeviceIoControl.IoControlCode)
    {
    case DEVICE_SEND: //send data from app to kernel driver
        KdPrint(("Send data is %ws \r\n", buffer));
        returnLength = (wcsnlen(buffer, 511) + 1) * 2;
        break;
    case DEVICE_REC: //receivce data from kernel driver to app
        wcsncpy(buffer, demo, 511);
        returnLength = (wcsnlen(buffer, 511) + 1) * 2;
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