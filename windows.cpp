#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <random>
#include <string>

struct SharedData {
    LONG counter;
};

constexpr wchar_t MAP_NAME[] = L"Local\\Lab6SharedCounterMap";
constexpr wchar_t SEM_NAME[] = L"Local\\Lab6SharedCounterSemaphore";

int worker(int id) {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MAP_NAME);
    if (!mapping) { std::cerr << "OpenFileMapping failed: " << GetLastError() << '\n'; return 1; }
    auto* shared = static_cast<SharedData*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData)));
    if (!shared) { std::cerr << "MapViewOfFile failed\n"; CloseHandle(mapping); return 1; }
    HANDLE semaphore = OpenSemaphoreW(SEMAPHORE_MODIFY_STATE | SYNCHRONIZE, FALSE, SEM_NAME);
    if (!semaphore) { std::cerr << "OpenSemaphore failed: " << GetLastError() << '\n'; UnmapViewOfFile(shared); CloseHandle(mapping); return 1; }

    std::mt19937 rng(std::random_device{}() ^ GetCurrentProcessId());
    std::uniform_int_distribution<int> coin(1, 2);
    bool ok = true;
    while (true) {
        DWORD result = WaitForSingleObject(semaphore, INFINITE);
        if (result != WAIT_OBJECT_0) { std::cerr << "Semaphore wait failed\n"; ok = false; break; }
        if (shared->counter >= 1000) {
            ReleaseSemaphore(semaphore, 1, nullptr);
            break;
        }
        while (shared->counter < 1000 && coin(rng) == 2) {
            ++shared->counter;
            std::cout << "Proces " << id << " (PID " << GetCurrentProcessId()
                      << "): " << shared->counter << '\n';
        }
        if (!ReleaseSemaphore(semaphore, 1, nullptr)) { std::cerr << "ReleaseSemaphore failed\n"; ok = false; break; }
        Sleep(1);
    }
    CloseHandle(semaphore);
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    return ok ? 0 : 1;
}

int main(int argc, char* argv[]) {
    if (argc == 3 && std::string(argv[1]) == "--worker") {
        return worker(std::stoi(argv[2]));
    }

    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        sizeof(SharedData), MAP_NAME);
    if (!mapping) { std::cerr << "CreateFileMapping failed: " << GetLastError() << '\n'; return 1; }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        std::cerr << "Maparea exista deja. Inchide alta instanta a programului.\n";
        CloseHandle(mapping); return 1;
    }
    auto* shared = static_cast<SharedData*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData)));
    if (!shared) { std::cerr << "MapViewOfFile failed\n"; CloseHandle(mapping); return 1; }
    shared->counter = 0;
    HANDLE semaphore = CreateSemaphoreW(nullptr, 1, 1, SEM_NAME);
    if (!semaphore) { std::cerr << "CreateSemaphore failed\n"; UnmapViewOfFile(shared); CloseHandle(mapping); return 1; }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        std::cerr << "Semaforul exista deja. Inchide alta instanta a programului.\n";
        CloseHandle(semaphore); UnmapViewOfFile(shared); CloseHandle(mapping); return 1;
    }

    char executable[MAX_PATH]{};
    DWORD n = GetModuleFileNameA(nullptr, executable, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        std::cerr << "GetModuleFileName failed\n";
        CloseHandle(semaphore); UnmapViewOfFile(shared); CloseHandle(mapping); return 1;
    }

    PROCESS_INFORMATION children[2]{};
    int created = 0;
    for (int i = 0; i < 2; ++i) {
        std::string cmd = "\"" + std::string(executable) + "\" --worker " + std::to_string(i + 1);
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessA(executable, cmd.data(), nullptr, nullptr, FALSE, 0,
                            nullptr, nullptr, &si, &pi)) {
            std::cerr << "CreateProcess failed: " << GetLastError() << '\n';
            break;
        }
        children[created++] = pi;
    }

    bool ok = (created == 2);
    for (int i = 0; i < created; ++i) {
        WaitForSingleObject(children[i].hProcess, INFINITE);
        DWORD code = 1;
        if (!GetExitCodeProcess(children[i].hProcess, &code) || code != 0) ok = false;
        CloseHandle(children[i].hProcess);
        CloseHandle(children[i].hThread);
    }
    std::cout << "Valoare finala: " << shared->counter << '\n';
    CloseHandle(semaphore);
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    return ok ? 0 : 1;
}
