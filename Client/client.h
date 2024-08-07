#ifndef KM_H
#define KM_H

#include "utils.h"

inline void FontColor(WORD color=0x07) { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color); }

#define LOG_RED(...) \
printf("[drvscan] "); \
FontColor(4); \
printf(__VA_ARGS__); \
FontColor(7); \

#define LOG_YELLOW(...) \
printf("[drvscan] "); \
FontColor(14); \
printf(__VA_ARGS__); \
FontColor(7); \

#define PRINT_RED(...) \
FontColor(4); \
printf(__VA_ARGS__); \
FontColor(7); \

#define PRINT_GREEN(...) \
FontColor(2); \
printf(__VA_ARGS__); \
FontColor(7); \

#define PRINT_BLUE(...) \
FontColor(3); \
printf(__VA_ARGS__); \
FontColor(7); \


#define LOG_DEBUG(...) \
FontColor(3); \
printf("[debug] "); \
FontColor(7); \
printf(__VA_ARGS__); \

#define DEBUG
#define LOG(...) printf("[drvscan] "  __VA_ARGS__)
#ifdef DEBUG
#define DEBUG_LOG(...) printf("[drvscan] " __VA_ARGS__)
#else
#define DEBUG_LOG(...) // __VA_ARGS__
#endif

typedef struct {
	QWORD                 Type;
	QWORD                 PhysicalStart;
	QWORD                 VirtualStart;
	UINT64                NumberOfPages;
	UINT64                Attribute;
} EFI_MEMORY_DESCRIPTOR;

namespace cl
{
	class client
	{
	public:
		//
		// initialize object
		//
		virtual BOOL  initialize(void) = 0;
		virtual BOOL  read_virtual(DWORD pid, QWORD address, PVOID buffer, QWORD length) = 0;
		virtual BOOL  write_virtual(DWORD pid, QWORD address, PVOID buffer, QWORD length) = 0;
		virtual BOOL  read_mmio(QWORD address, PVOID buffer, QWORD length) = 0;
		virtual BOOL  write_mmio(QWORD address, PVOID buffer, QWORD length) = 0;
		virtual BOOL  read_pci (BYTE bus, BYTE slot, BYTE func, DWORD offset, PVOID buffer, DWORD length) = 0;
		virtual BOOL  write_pci(BYTE bus, BYTE slot, BYTE func, DWORD offset, PVOID buffer, DWORD length) = 0;
		virtual QWORD get_physical_address(QWORD virtual_address) = 0;
		virtual std::vector<EFI_MEMORY_DESCRIPTOR> get_memory_map() = 0;
	};
}


typedef struct
{
	QWORD PhysicalStart;
	QWORD NumberOfPages;
} EFI_PAGE_TABLE_ALLOCATION;

typedef struct {
	QWORD virtual_address;
	QWORD physical_address;
	DWORD size;
} EFI_MODULE_INFO;

typedef struct {
	BYTE  bus,slot,func;
	config::Pci cfg;
	QWORD pci_device_object;
	QWORD drv_device_object;
} DEVICE_INFO;

typedef struct {
	unsigned char  blk;       // is port blocked
	unsigned char  blk_info;  // reason for blocking
	DEVICE_INFO    self;      // self data
	std::vector<DEVICE_INFO> devices;
} PORT_DEVICE_INFO;

#define DMP_FULL     0x0001
#define DMP_CODEONLY 0x0002
#define DMP_READONLY 0x0004
#define DMP_RAW      0x0008
#define DMP_RUNTIME  0x0010

namespace cl
{
	BOOL initialize(void);

	QWORD get_physical_address(QWORD virtual_address);
	QWORD get_virtual_address(QWORD physical_address);

	namespace vm
	{
		BOOL  read(DWORD pid, QWORD address, PVOID buffer, QWORD length);
		PVOID dump_module(DWORD pid, QWORD base, DWORD dmp_type);
		void  free_module(PVOID dumped_module);

		template <typename t>
		t read(DWORD pid, QWORD address)
		{
			t b;
			if (!read(pid, address, &b, sizeof(b)))
			{
				b = 0;
			}
			return b;
		}

		inline QWORD get_relative_address(DWORD pid, QWORD address, INT offset, INT instruction_size)
		{
			return (address + instruction_size) + vm::read<INT>(pid, address + offset);
		}
	}

	namespace io
	{
		BOOL read(QWORD address, PVOID buffer, QWORD length);
		BOOL write(QWORD address, PVOID buffer, QWORD length);
		template <typename t>
		t read(QWORD address)
		{
			t b;
			if (!read(address, &b, sizeof(b)))
			{
				b = 0;
			}
			return b;
		}
		template <typename t>
		BOOL write(QWORD address, t value)
		{
			return write(address, &value, sizeof(t));
		}
	}

	namespace pci
	{
		QWORD get_physical_address(ULONG bus, ULONG slot);
		BOOL  read(BYTE bus, BYTE slot, BYTE func, DWORD offset, PVOID buffer, DWORD size);
		BOOL  write(BYTE bus, BYTE slot, BYTE func, DWORD offset, PVOID buffer, DWORD size);

		template <typename t>
		t read(BYTE bus, BYTE slot, BYTE func, DWORD offset)
		{
			t b;
			if (!read(bus, slot, func, offset, &b, sizeof(b)))
			{
				b = 0;
			}
			return b;
		}

		template <typename t>
		BOOL write(BYTE bus, BYTE slot, BYTE func, DWORD offset, t value)
		{
			return write(bus, slot,func, offset, &value, sizeof(t));
		}

		//
		// gets every active port from the system with devices
		//
		std::vector<PORT_DEVICE_INFO> get_port_devices(void);
	}

	namespace efi
	{
		//
		// KeLoaderBlock EfiMemoryMap
		//
		std::vector<EFI_MEMORY_DESCRIPTOR> get_memory_map();

		//
		// get list of runtime DXE modules loaded by motherboard BIOS.rom
		//
		std::vector<EFI_MODULE_INFO> get_dxe_modules(std::vector<EFI_MEMORY_DESCRIPTOR> &memory_map);

		//
		// every dxe module is loaded in one big memory range, we can resolve it by giving any module information
		//
		EFI_MEMORY_DESCRIPTOR get_dxe_range(
			EFI_MODULE_INFO module,
			std::vector<EFI_MEMORY_DESCRIPTOR> &page_table_list
			);

		//
		// get list of efi runtime functions
		//
		std::vector<QWORD> get_runtime_table(void);
	}


	extern QWORD ntoskrnl_base;
	extern std::vector<QWORD> global_export_list;
}

class DLL_EXPORT
{
	QWORD address;
public:
	DLL_EXPORT(QWORD address) : address(address)
	{
		cl::global_export_list.push_back((QWORD)&this->address);
	}
	operator QWORD () const { return address; }

};

#define NTOSKRNL_EXPORT(export_name) \
DLL_EXPORT export_name((QWORD)#export_name);

#define EXTERN_NTOSKRNL_EXPORT(export_name) \
extern DLL_EXPORT export_name;

EXTERN_NTOSKRNL_EXPORT(HalPrivateDispatchTable);
EXTERN_NTOSKRNL_EXPORT(PsInitialSystemProcess);
EXTERN_NTOSKRNL_EXPORT(PsGetProcessId);
EXTERN_NTOSKRNL_EXPORT(KeQueryPrcbAddress);
EXTERN_NTOSKRNL_EXPORT(HalEnumerateEnvironmentVariablesEx);
EXTERN_NTOSKRNL_EXPORT(MmGetVirtualForPhysical);
EXTERN_NTOSKRNL_EXPORT(ExAllocatePool2);
EXTERN_NTOSKRNL_EXPORT(ExFreePool);
EXTERN_NTOSKRNL_EXPORT(MmGetPhysicalAddress);
EXTERN_NTOSKRNL_EXPORT(MmIsAddressValid);

namespace kernel
{
	EXTERN_NTOSKRNL_EXPORT(memcpy);
}

#endif /* KM_H */

