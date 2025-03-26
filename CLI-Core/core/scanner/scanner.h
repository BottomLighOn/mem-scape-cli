#ifndef SCANNER_H
#define SCANNER_H
#include <Windows.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>

struct memory_region
{
    uintptr_t start_adress;
    size_t size;
    DWORD protection;
};

template<typename T>
struct scanned_value
{
    T value;
    uintptr_t address;
};

class scanner
{
    DWORD attached_pid;
    HANDLE attached_handle;
    std::vector<memory_region> scanned_regions;
    std::vector<scanned_value<int>> scanned_ints;
    std::mutex results_mutex;
    static const int num_threads = 4;
    void search_int_thread(int value, size_t start_idx, size_t end_idx);
    void filter_int_thread(int value, size_t start_idx, size_t end_idx, const std::vector<scanned_value<int>>& source_values);
public:
    static scanner* instance() {
        static scanner singleton;
        return &singleton;
    }
    void setup(DWORD pid, HANDLE handle);
    void reset();
    void scan_regions();
    void print_regions();
    void print_scanned_ints();
    int get_scanned_count();
    void search_int(int value);
    void filter_int(int value);
    bool is_avx_instructions_supported();
    template<typename T>
    void search(T value) {
        if (std::is_same<T, int>::value) {
            search_int(value);
        }
    }
};
#endif // !SCANNER_H
