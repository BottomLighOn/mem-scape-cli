#include "scanner.h"
#include <iostream>
#include <thread>
#include <algorithm>

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
    
    scanned_ints.clear();
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
    const size_t buffer_size = 32768;
    std::vector<int> buffer(buffer_size / sizeof(int));

    const size_t CACHE_SIZE = 16384;
    scanned_value<int>* cache_buffer = new scanned_value<int>[CACHE_SIZE];
    memset(cache_buffer, 0, sizeof(scanned_value<int>[CACHE_SIZE]));

    size_t cache_count = 0;

    std::vector<scanned_value<int>> local_results;
    local_results.reserve(1000000);

    auto flush_cache = [&] () {
        if (cache_count > 0) {
            if (local_results.size() + cache_count > local_results.capacity()) {
                local_results.reserve(max(local_results.capacity() * 2,
                                      local_results.size() + cache_count));
            }

            local_results.insert(local_results.end(),
                                 cache_buffer,
                                 cache_buffer + cache_count);

            cache_count = 0;
        }
    };

    for (size_t i = start_idx; i < end_idx; i++) {
        auto& region = scanned_regions[i];
        if (region.protection & (PAGE_EXECUTE_READWRITE | PAGE_EXECUTE)) {
            continue;
        }

        uintptr_t base_address = region.start_adress;
        uintptr_t end_address = region.start_adress + region.size;

        while (base_address < end_address) {
            size_t bytes_to_read = min(buffer_size, static_cast<size_t>(end_address - base_address));
            size_t bytes_read = 0;

            if (ReadProcessMemory(attached_handle, (void*)base_address, buffer.data(),
                bytes_to_read, &bytes_read)) {

                size_t ints_read = bytes_read / sizeof(int);
                int* data = buffer.data();

                for (size_t j = 0; j < ints_read; j++) {
                    if (data[j] == value) {
                        cache_buffer[cache_count++] = {data[j], base_address + j * sizeof(int)};

                        if (cache_count == CACHE_SIZE) {
                            flush_cache();
                        }
                    }
                }
            }
            base_address += bytes_read;
        }
    }

    flush_cache();

    if (!local_results.empty()) {
        std::lock_guard<std::mutex> lock(results_mutex);
        scanned_ints.insert(scanned_ints.end(),
                            std::make_move_iterator(local_results.begin()),
                            std::make_move_iterator(local_results.end()));
    }


    delete[] cache_buffer;
}


void scanner::filter_int(int value) {
    std::vector<scanned_value<int>> scanned_ints_local = std::move(scanned_ints);
    scanned_ints.clear();

    size_t values_per_thread = (scanned_ints_local.size() + num_threads - 1) / num_threads;
    std::vector<std::thread> local_threads;

    for (size_t i = 0; i < num_threads; i++) {
        size_t start_idx = i * values_per_thread;
        size_t end_idx = min((i + 1) * values_per_thread, scanned_ints_local.size());

        if (start_idx >= scanned_ints_local.size())
            break;

        local_threads.emplace_back(&scanner::filter_int_thread, this, value, start_idx, end_idx, std::ref(scanned_ints_local));
    }

    for (auto& thread : local_threads) {
        thread.join();
    }
}

void scanner::filter_int_thread(int value, size_t start_idx, size_t end_idx, const std::vector<scanned_value<int>>& source_values) {
    const size_t CACHE_SIZE = 16384;
    scanned_value<int>* cache_buffer = new scanned_value<int>[CACHE_SIZE];
    memset(cache_buffer, 0, sizeof(scanned_value<int>[CACHE_SIZE]));
    size_t cache_count = 0;

    std::vector<scanned_value<int>> local_results;
    local_results.reserve((end_idx - start_idx) / 3); 

    auto flush_cache = [&] () {
        if (cache_count > 0) {
            local_results.insert(local_results.end(), cache_buffer, cache_buffer + cache_count);
            cache_count = 0;
        }
    };

    const size_t BLOCK_SIZE = 4096; 
    int* block_buffer = new int[BLOCK_SIZE / sizeof(int)];
    memset(block_buffer, 0, sizeof(int[BLOCK_SIZE / sizeof(int)]));
    
    struct PageCache
    {
        uintptr_t page_addr;
        int data[BLOCK_SIZE / sizeof(int)];
        bool valid;
    };

    const size_t PAGE_CACHE_SIZE = 32;
    PageCache* page_cache = new PageCache[PAGE_CACHE_SIZE];
    memset(page_cache, 0, sizeof(PageCache[PAGE_CACHE_SIZE]));
    size_t cache_next = 0;

    for (size_t i = 0; i < PAGE_CACHE_SIZE; i++) {
        page_cache[i].valid = false;
        page_cache[i].page_addr = 0;
    }

    auto get_page_data = [&] (uintptr_t addr) -> int* {
        uintptr_t page_addr = addr & ~(BLOCK_SIZE - 1);

        for (size_t i = 0; i < PAGE_CACHE_SIZE; i++) {
            if (page_cache[i].valid && page_cache[i].page_addr == page_addr) {
                return page_cache[i].data;
            }
        }

        PageCache& new_cache = page_cache[cache_next];
        cache_next = (cache_next + 1) % PAGE_CACHE_SIZE;

        new_cache.valid = false;
        new_cache.page_addr = page_addr;

        size_t bytes_read = 0;
        if (ReadProcessMemory(attached_handle, (void*)page_addr, new_cache.data,
            BLOCK_SIZE, &bytes_read) && bytes_read > 0) {
            new_cache.valid = true;
            return new_cache.data;
        }

        return nullptr;
    };

    for (size_t i = start_idx; i < end_idx; i++) {
        uintptr_t addr = source_values[i].address;

        int* page_data = get_page_data(addr);

        if (page_data) {
            size_t offset = (addr & (BLOCK_SIZE - 1)) / sizeof(int);

            if (page_data[offset] == value) {
                cache_buffer[cache_count++] = {value, addr};

                if (cache_count == CACHE_SIZE) {
                    flush_cache();
                }
            }
        }
    }

    flush_cache();

    if (!local_results.empty()) {
        std::lock_guard<std::mutex> lock(results_mutex);
        scanned_ints.insert(scanned_ints.end(),
                            std::make_move_iterator(local_results.begin()),
                            std::make_move_iterator(local_results.end()));

    }
    delete[] block_buffer;
    delete[] page_cache;
    delete[] cache_buffer;
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