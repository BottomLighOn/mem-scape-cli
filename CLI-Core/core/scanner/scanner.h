#ifndef SCANNER_H
#define SCANNER_H
#include <Windows.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>

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
    void search_int(int value);
    void filter_int(int value);
    template<typename T>
    void search(T value) {
        if (std::is_same<T, int>::value) {
            search_int(value);
        }
    }
};
#endif // !SCANNER_H
