#include "scan.h"

namespace scan
{
	static void compare_sections(QWORD local_image, QWORD runtime_image, DWORD diff);
	static BOOL dump_module_to_file(std::vector<FILE_INFO> &modules, DWORD pid, FILE_INFO file);
	static void scan_w32khooks(QWORD win32k_dmp, FILE_INFO &win32k, std::vector<FILE_INFO> &modules);
	static void scan_krnlhooks(QWORD ntoskrnl_dmp, std::vector<FILE_INFO> &modules);
}

void scan::image(BOOL save_cache, std::vector<FILE_INFO> modules, DWORD pid, FILE_INFO file, BOOL use_cache)
{
	if (save_cache)
	{
		dump_module_to_file(modules, pid, file);
		return;
	}

	//
	// dump image
	//
	QWORD runtime_image = (QWORD)cl::vm::dump_module(pid, file.base, DMP_FULL | DMP_RUNTIME);
	if (runtime_image == 0)
	{
		LOG_RED("failed to scan %s\n", file.path.c_str());
		return;
	}

	if (pid == 4 || pid == 0)
	{
		if (!_strcmpi(file.name.c_str(), "win32k.sys"))
		{
			scan_w32khooks(runtime_image, file, modules);
		}

		if (!_strcmpi(file.name.c_str(), "ntoskrnl.exe"))
		{
			scan_krnlhooks(runtime_image, modules);
		}
	}


	//
	// try to use existing memory dumps
	//
	HMODULE local_image = 0;

	if (use_cache)
	{
		local_image = (HMODULE)LoadImageEx(("./dumps/" + file.name).c_str(), 0, file.base, runtime_image);
		if (local_image == 0)
		{
			local_image = (HMODULE)LoadImageEx(file.path.c_str(), 0, file.base, runtime_image);
		}
	}
	else
	{
		const char *sub_str = strstr(file.path.c_str(), "\\dump_");

		if (sub_str)
		{
			std::string sub_name = sub_str + 6;
			std::string resolved_path;

			for (auto &lookup : modules)
			{
				if (!_strcmpi(lookup.name.c_str(), sub_name.c_str()))
				{
					resolved_path = lookup.path;
				}
			}

			if (resolved_path.size() < 1)
			{
				resolved_path = "C:\\Windows\\System32\\Drivers\\" + sub_name;
			}

			file.path = resolved_path;
		}

		local_image = (HMODULE)LoadImageEx(file.path.c_str(), 0, file.base, runtime_image);
	}

	if (local_image == 0)
	{
		LOG_RED("failed to scan %s\n", file.path.c_str());
		cl::vm::free_module((PVOID)runtime_image);
		return;
	}

	DWORD min_difference = 1;

	LOG("scanning: %s\n", file.path.c_str());

	compare_sections((QWORD)local_image, runtime_image, min_difference);

	cl::vm::free_module((PVOID)runtime_image);

	FreeImageEx((void *)local_image);
}

QWORD get_dump_export(PVOID dumped_module, PCSTR export_name)
{
	QWORD a0;
	DWORD a1[4]{};


	QWORD base = (QWORD)dumped_module;


	a0 = base + *(WORD*)(base + 0x3C);
	if (a0 == base)
	{
		return 0;
	}

	DWORD wow64_off = *(WORD*)(a0 + 0x4) == 0x8664 ? 0x88 : 0x78;

	a0 = base + (QWORD)*(DWORD*)(a0 + wow64_off);
	if (a0 == base)
	{
		return 0;
	}

	static int cnt=0;
	cnt++;

	memcpy(&a1, (const void *)(a0 + 0x18), sizeof(a1));
	while (a1[0]--)
	{
		a0 = (QWORD)*(DWORD*)(base + a1[2] + ((QWORD)a1[0] * 4));
		if (a0 == 0)
		{
			continue;
		}

		if (!_strcmpi((const char*)(base + a0), export_name))
		{
			a0 = *(WORD*)(base + a1[3] + ((QWORD)a1[0] * 2)) * 4;
			a0 = *(DWORD*)(base + a1[1] + a0);
			return (QWORD)((QWORD)dumped_module + a0);
		}
	}
	return 0;
}

QWORD find_table_by_function(QWORD win32k, QWORD win32k_dmp, QWORD Win32kApiSetTable, QWORD func)
{
	for (int i = 0; i < 60; i++)
	{
		QWORD table = *(QWORD*)(Win32kApiSetTable + (i * sizeof(QWORD)));
		if (table == 0)
			continue;

		table = table - win32k;
		table = table + win32k_dmp;
		if (*(QWORD*)table == func)
		{
			return table;
		}
	}
	return 0;
}

static void scan::scan_w32khooks(QWORD win32k_dmp, FILE_INFO &win32k, std::vector<FILE_INFO> &modules)
{
	FILE_INFO win32kfull{};

	for (auto &mod : modules)
	{
		if (!_strcmpi(mod.name.c_str(), "win32kfull.sys"))
		{
			win32kfull = mod;
			break;
		}
	}

	if (win32kfull.base == 0)
		return;

	QWORD Win32kApiSetTable = get_dump_export((PVOID)win32k_dmp, "ext_ms_win_moderncore_win32k_base_sysentry_l1_table");
	Win32kApiSetTable =  Win32kApiSetTable + 0x70;

	QWORD win32kfull_dmp = (QWORD)cl::vm::dump_module(4, win32kfull.base, DMP_FULL | DMP_RUNTIME);
	QWORD *table0  = (QWORD*)get_dump_export((PVOID)win32kfull_dmp, "ext_ms_win_core_win32k_fulluser_l1_table");
	QWORD *table1  = (QWORD*)find_table_by_function(win32k.base, win32k_dmp, Win32kApiSetTable, *(QWORD*)table0);
	QWORD lastadr = (QWORD)table0 + win32kfull.base - (QWORD)win32kfull_dmp;

	// LOG("scanning win32k hooks\n");

	int index=0;
	while (1)
	{
		if (table0[index] == lastadr)
		{
			break;
		}

		if (table1[index] < win32kfull.base || table1[index] > (win32kfull.base + win32kfull.size))
		{
			LOG("win32k hook [%d] [%llX]\n", index, table1[index]);
		}

		if (table0[index] != table1[index])
		{
			LOG("win32k hook [%d] [%llX]\n", index, table1[index]);
		}

		index++;
	}

	// LOG("ext_ms_win_core_win32k_fulluser_l1_table total entries: %ld\n", index);

	index   = 0;
	table0  = (QWORD*)get_dump_export((PVOID)win32kfull_dmp, "ext_ms_win_core_win32k_fullgdi_l1_table");
	table1  = (QWORD*)find_table_by_function(win32k.base, win32k_dmp, Win32kApiSetTable, *(QWORD*)table0);
	lastadr = (QWORD)table0 + win32kfull.base - (QWORD)win32kfull_dmp;
	while (1)
	{
		if (table0[index] == lastadr)
		{
			break;
		}

		if (table1[index] < win32kfull.base || table1[index] > (win32kfull.base + win32kfull.size))
		{
			LOG("win32k hook [%d] [%llX]\n", index, table1[index]);
		}

		if (table0[index] != table1[index])
		{
			LOG("win32k hook [%d] [%llX]\n", index, table1[index]);
		}

		index++;
	}

	// LOG("ext_ms_win_core_win32k_fullgdi_l1_table total entries: %ld\n", index);

	cl::vm::free_module((PVOID)win32kfull_dmp);
}

static void scan::scan_krnlhooks(QWORD ntoskrnl_dmp, std::vector<FILE_INFO> &modules)
{
	LOG("scanning ntoskrnl hooks\n");
	QWORD *table = (QWORD*)get_dump_export((PVOID)ntoskrnl_dmp, "HalPrivateDispatchTable");


	std::vector<FILE_INFO> valid_modules;
	for (auto &mod : modules)
	{
		if (!_strcmpi(mod.name.c_str(), "ntoskrnl.exe"))
		{
			valid_modules.push_back(mod);
		}
		else if (!_strcmpi(mod.name.c_str(), "pci.sys"))
		{
			valid_modules.push_back(mod);
		}
		else if (!_strcmpi(mod.name.c_str(), "ACPI.sys"))
		{
			valid_modules.push_back(mod);
		}
		else if (!_strcmpi(mod.name.c_str(), "hal.dll"))
		{
			valid_modules.push_back(mod);
		}

		if (valid_modules.size() == 4)
		{
			break;
		}
	}

	int index = 1;
	while (1)
	{
		if (table[index] == 0 && table[index + 1] == 0 && index > 100)
			break;

		QWORD table_address = table[index];

		if (table_address)
		{
			BOOL found = 0;
			for (auto &mod : valid_modules)
			{

				if (table_address >= mod.base &&
					table_address <= (mod.base + mod.size)
					)
				{
					found = 1;
					break;
				}
			}

			if (!found)
			{
				LOG("HalPrivateDispatchTable hook [%ld] [%llx]\n", index, table_address);
			}
		}

		index++;
	}

	table = (QWORD*)get_dump_export((PVOID)ntoskrnl_dmp, "HalDispatchTable");
	index = 1;
	while (1)
	{
		if (table[index] < 0xffff800000000000 && index > 20)
			break;

		QWORD table_address = table[index];
		if (table_address)
		{
			BOOL found = 0;
			for (auto &mod : valid_modules)
			{

				if (table_address >= mod.base &&
					table_address <= (mod.base + mod.size)
					)
				{
					found = 1;
					break;
				}
			}

			if (!found)
			{
				LOG("HalDispatchTable hook [%ld] [%llx]\n", index, table_address);
			}
		}
		index++;
	}
}

static void scan_section(DWORD diff, CHAR *section_name, QWORD local_image, QWORD runtime_image, QWORD size, QWORD section_address)
{
	for (QWORD i = 0; i < size; i++)
	{
		if (((unsigned char*)local_image)[i] == ((unsigned char*)runtime_image)[i])
		{
			continue;
		}

		DWORD cnt = 0;
		while (1)
		{

			if (i + cnt >= size)
			{
				break;
			}

			if (((unsigned char*)local_image)[i + cnt] == ((unsigned char*)runtime_image)[i + cnt])
			{
				break;
			}

			cnt++;
		}
		if (cnt >= diff)
		{
			printf("%s:0x%llx is modified (%ld bytes): ", section_name, section_address + i, cnt);
			for (DWORD j = 0; j < cnt; j++)
			{
				PRINT_GREEN("%02X ", ((unsigned char*)local_image)[i + j]);
			}
			printf("-> ");
			for (DWORD j = 0; j < cnt; j++)
			{
				PRINT_RED("%02X ", ((unsigned char*)runtime_image)[i + j]);
			}
			printf("\n");
		}
		i += cnt;
	}
}

static void scan::compare_sections(QWORD local_image, QWORD runtime_image, DWORD diff)
{
	QWORD image_dos_header = (QWORD)local_image;
	QWORD image_nt_header = *(DWORD*)(image_dos_header + 0x03C) + image_dos_header;
	unsigned short machine = *(WORD*)(image_nt_header + 0x4);

	QWORD section_header_off = machine == 0x8664 ?
		image_nt_header + 0x0108 :
		image_nt_header + 0x00F8;

	for (WORD i = 0; i < *(WORD*)(image_nt_header + 0x06); i++) {
		QWORD section = section_header_off + (i * 40);
		ULONG section_characteristics = *(ULONG*)(section + 0x24);

		UCHAR *section_name = (UCHAR*)(section + 0x00);
		ULONG section_virtual_address = *(ULONG*)(section + 0x0C);
		ULONG section_virtual_size = *(ULONG*)(section + 0x08);

		if (section_characteristics & 0x00000020 && !(section_characteristics & 0x02000000))
		{
			//
			// skip Warbird page
			//
			if (!strcmp((const char*)section_name, "PAGEwx3"))
			{
				continue;
			}
		
			scan_section(
				diff,
				(CHAR*)section_name,
				(QWORD)((BYTE*)local_image + section_virtual_address),
				(QWORD)(runtime_image + section_virtual_address),
				section_virtual_size,
				section_virtual_address
			);
		}
	}
}

static BOOL write_dump_file(std::string name, PVOID buffer, QWORD size)
{
	if (CreateDirectoryA("./dumps/", NULL) || ERROR_ALREADY_EXISTS == GetLastError())
	{
		std::string path = "./dumps/" + name;
		FILE* f = fopen(path.c_str(), "wb");

		if (f)
		{
			fwrite(buffer, size, 1, f);

			fclose(f);

			return 1;
		}
	}

	return 0;
}

static BOOL scan::dump_module_to_file(std::vector<FILE_INFO> &modules, DWORD pid, FILE_INFO file)
{
	const char *sub_str = strstr(file.path.c_str(), "\\dump_");

	if (sub_str)
	{
		std::string sub_name = sub_str + 6;
		std::string resolved_path;

		for (auto &lookup : modules)
		{
			if (!_strcmpi(lookup.name.c_str(), sub_name.c_str()))
			{
				resolved_path = lookup.path;
			}
		}

		if (resolved_path.size() < 1)
		{
			resolved_path = "C:\\Windows\\System32\\Drivers\\" + sub_name;
		}

		file.path = resolved_path;
	}

	PVOID disk_base = (PVOID)LoadFileEx(file.path.c_str(), 0);
	if (disk_base == 0)
	{
		return 0;
	}

	QWORD target_base = (QWORD)cl::vm::dump_module(pid, file.base, DMP_FULL | DMP_RAW);
	if (target_base == 0)
	{
		free(disk_base);
		return FALSE;
	}

	//
	// copy discardable sections from disk
	//
	QWORD disk_nt = (QWORD)pe::get_nt_headers((QWORD)disk_base);
	PIMAGE_SECTION_HEADER section_disk = pe::nt::get_image_sections(disk_nt);
	for (WORD i = 0; i < pe::nt::get_section_count(disk_nt); i++)
	{
		if (section_disk[i].SizeOfRawData)
		{
			if ((section_disk[i].Characteristics & 0x02000000))
			{
				memcpy(
					(void*)(target_base + section_disk[i].PointerToRawData),
					(void*)((QWORD)disk_base + section_disk[i].PointerToRawData),
					section_disk[i].SizeOfRawData
				);
			}
		}
	}

	//
	// free disk base
	//
	free(disk_base);

	//
	// write dump file to /dumps/modulename
	//
	BOOL status = write_dump_file(file.name.c_str(), (PVOID)target_base, *(QWORD*)(target_base - 16 + 8));

	if (status)
		LOG("module %s is succesfully cached\n", file.name.c_str());
	cl::vm::free_module((PVOID)target_base);

	return status;
}
