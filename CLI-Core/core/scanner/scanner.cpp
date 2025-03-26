#include "scanner.h"
#include <iostream>
#include <thread>

void scanner::setup(DWORD pid, HANDLE handle) {
    attached_pid = pid;
    attached_handle = handle;
}

void scanner::reset() {
    attached_pid = 0;
    attached_handle = 0;
    scanned_regions.clear();
    scanned_ints.clear();
}

void scanner::scan_regions() {
    scanned_regions.clear();
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    uintptr_t base_address = 0;
    while (VirtualQueryEx(attached_handle, (LPCVOID)base_address, &mbi, sizeof(MEMORY_BASIC_INFORMATION))) {
        if ((mbi.State == MEM_COMMIT) &&
            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE)) &&
            !(mbi.Protect & PAGE_GUARD)
            ) {
            memory_region current_region;
            memset(&current_region, 0, sizeof(current_region));
            current_region.start_adress = (uintptr_t)mbi.BaseAddress;
            current_region.protection = mbi.Protect;
            current_region.size = mbi.RegionSize;
            scanned_regions.push_back(current_region);
        }
        base_address = ((uintptr_t)mbi.BaseAddress + mbi.RegionSize);
    }
}

void scanner::print_regions() {
    std::cout << "Scanned region count: " << scanned_regions.size() << std::endl;
    for (auto& region : scanned_regions) {
        std::cout << "[0x" << (void*)region.start_adress << "] Size: " << region.size << " Protect: ";
        if (region.protection & PAGE_READONLY) std::cout << "R";
        if (region.protection & PAGE_READWRITE) std::cout << "RW";
        if (region.protection & PAGE_EXECUTE) std::cout << "X";
        if (region.protection & PAGE_EXECUTE_READ) std::cout << "RX";
        if (region.protection & PAGE_EXECUTE_READWRITE) std::cout << "RWX";
        std::cout << std::endl;
    }
}

void scanner::search_int(int value){
    if (scanned_regions.empty()) {
        std::cout << "No regions found" << std::endl;
        return;
    }
    
    size_t regions_per_thread = (scanned_regions.size() + num_threads - 1) / num_threads;
    std::vector<std::thread> local_threads;

    for (size_t i = 0; i < num_threads; i++) {
        size_t start_idx = i * regions_per_thread;
        size_t end_idx = min((i + 1) * regions_per_thread, scanned_regions.size());

        if (start_idx >= scanned_regions.size())
            break;

        local_threads.emplace_back(&scanner::search_int_thread, this, value, start_idx, end_idx);
    }

    for (auto& thread : local_threads) {
        thread.join();
    }
}

void scanner::search_int_thread(int value, size_t start_idx, size_t end_idx) {
    const size_t buffer_size = 4096;
    std::vector<int> buffer(buffer_size / sizeof(int));
    std::vector<scanned_value<int>> local_results;

    for (size_t i = start_idx; i < end_idx; i++) {
        auto& region = scanned_regions[i];
        if (region.protection & (PAGE_EXECUTE_READWRITE | PAGE_EXECUTE)) {
            continue;
        }

        uintptr_t base_address = region.start_adress;
        uintptr_t end_address = region.start_adress + region.size;

        while (base_address < end_address) {
            size_t bytes_read = 0;
            if (ReadProcessMemory(attached_handle, (void*)base_address, buffer.data(),
                buffer.size() * sizeof(int), &bytes_read)) {
                size_t ints_read = bytes_read / sizeof(int);
                for (int i = 0; i < ints_read; i++) {
                    if (buffer[i] == value) {
                        local_results.push_back({buffer[i], base_address + i * sizeof(int)});
                    }
                }
            }
            base_address += buffer.size() * sizeof(int);
        }
    }

    if (!local_results.empty()) {
        std::lock_guard<std::mutex> lock(results_mutex);
        scanned_ints.insert(scanned_ints.end(), local_results.begin(), local_results.end());
    }
}


void scanner::filter_int(int value) {
    std::vector<scanned_value<int>> scanned_ints_local = std::move(scanned_ints);
    int buffer = -1;
    for (auto& v : scanned_ints_local) {
        size_t bytes_read = 0;
        if (ReadProcessMemory(attached_handle, (void*)v.address, &buffer, sizeof(int), &bytes_read)) {
            if (buffer == value) {
                scanned_ints.push_back({buffer, v.address});
            }
        }
    }
}

void scanner::print_scanned_ints() {
    if (scanned_ints.empty()) {
        std::cout << "Scanned data empty" << std::endl;
        return;
    }

    for (auto& scanned_int : scanned_ints) {
        std::cout << "[0x" << (void*)scanned_int.address << "] " << scanned_int.value << std::endl;
    }
}

int scanner::get_scanned_count() {
    return scanned_ints.size();
}