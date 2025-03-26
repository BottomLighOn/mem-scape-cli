#ifndef MAPPER_H
#define MAPPER_H
#define DISABLE_OUTPUT
#include <string>
class mapper
{
    std::wstring driver_name = L"driver.sys";
public:
    static mapper* instance() {
        static mapper singleton;
        return &singleton;
    }
    bool mmap();
    void set_driver_name(const std::wstring& name);
};
#endif // !MAPPER_H
