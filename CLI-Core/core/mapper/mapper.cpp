#include "mapper.h"
#define DISABLE_OUTPUT
#include <kdmapper.hpp>
#pragma comment(lib, "kdmapper.lib")


bool callback(ULONG64* param1, ULONG64* param2, ULONG64 allocationPtr, ULONG64 allocationSize) {
	UNREFERENCED_PARAMETER(param1);
	UNREFERENCED_PARAMETER(param2);
	UNREFERENCED_PARAMETER(allocationPtr);
	UNREFERENCED_PARAMETER(allocationSize);
	Log("[+] Callback called" << std::endl);

	/*
	This callback occurs before call driver entry and
	can be useful to pass more customized params in
	the last step of the mapping procedure since you
	know now the mapping address and other things
	*/
	return true;
}

bool mapper::mmap() {
	HANDLE iqvw64e_device_handle = intel_driver::Load();

	if (iqvw64e_device_handle == INVALID_HANDLE_VALUE) {
		std::wcout << L"[mapper] Failed to load vulnerible driver" << std::endl;
		return false;
	}

	wchar_t buffer[MAX_PATH];
	DWORD length = GetCurrentDirectoryW(MAX_PATH, buffer);

	
	std::wstring driver_path = buffer + std::wstring(L"\\drivers\\") + driver_name;
	std::wcout << L"[mapper] Trying to load driver: " << driver_path << std::endl;

	std::vector<uint8_t> raw_image = {0};
	if (!utils::ReadFileToMemory(driver_path.c_str(), &raw_image)) {
		std::wcout << L"[mapper] Failed to read image to memory, or [file\\folder] does not exist. File: " << driver_path.c_str() << std::endl;
		intel_driver::Unload(iqvw64e_device_handle);
		return false;
	}

	kdmapper::AllocationMode mode = kdmapper::AllocationMode::AllocatePool;

	NTSTATUS exitCode = 0;
	if (!kdmapper::MapDriver(iqvw64e_device_handle, raw_image.data(), 0, 0, free, true, mode, false, callback, &exitCode)) {
		std::wcout << L"[mapper] Failed to map " << driver_path << std::endl;
		if (!intel_driver::Unload(iqvw64e_device_handle)) {
			std::wcout << L"[mapper] Warning failed to fully unload vulnerable driver, MMAP operation was failed." << std::endl;
		}
		return false;
	}

	if (!intel_driver::Unload(iqvw64e_device_handle)) {
		std::wcout << L"[mapper] Warning failed to fully unload vulnerable driver " << std::endl;
	}
	std::wcout << L"[mapper] Success" << std::endl;
	return true;
}

void mapper::set_driver_name(const std::wstring& name) {
	driver_name = name;
}