#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <sys/mman.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <unistd.h>

struct SharedData {
    sem_t mutex;
    int counter;
};

void worker(SharedData* shared, int workerId) {
    std::mt19937 rng(std::random_device{}() ^ static_cast<unsigned>(getpid()));
    std::uniform_int_distribution<int> coin(1, 2);

    while (true) {
        while (sem_wait(&shared->mutex) == -1) {
            if (errno != EINTR) {
                std::cerr << "sem_wait failed: " << std::strerror(errno) << '\n';
                _exit(1);
            }
        }

        // Doar un proces poate citi/modifica counter in acest moment.
        if (shared->counter >= 1000) {
            sem_post(&shared->mutex);
            break;
        }

        // Cat timp moneda da 2, crestem valoarea din memoria partajata.
        // Oprim si la 1000, chiar daca moneda continua sa dea 2.
        while (shared->counter < 1000 && coin(rng) == 2) {
            ++shared->counter;
            std::cout << "Proces " << workerId << " (PID " << getpid()
                      << "): " << shared->counter << std::endl;
        }

        if (sem_post(&shared->mutex) == -1) {
            std::cerr << "sem_post failed\n";
            _exit(1);
        }
        // Evitam ca un singur proces sa preia semaforul la nesfarsit.
        usleep(1000);
    }
    _exit(0);
}

int main() {
    void* mem = mmap(nullptr, sizeof(SharedData), PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        std::cerr << "mmap failed: " << std::strerror(errno) << '\n';
        return 1;
    }
    auto* shared = static_cast<SharedData*>(mem);
    shared->counter = 0;

    if (sem_init(&shared->mutex, 1, 1) == -1) {
        std::cerr << "sem_init failed: " << std::strerror(errno) << '\n';
        munmap(mem, sizeof(SharedData));
        return 1;
    }

    pid_t children[2]{};
    for (int i = 0; i < 2; ++i) {
        pid_t pid = fork();
        if (pid == -1) {
            std::cerr << "fork failed: " << std::strerror(errno) << '\n';
            // Daca al doilea fork esueaza, asteptam primul copil.
            for (int j = 0; j < i; ++j) waitpid(children[j], nullptr, 0);
            sem_destroy(&shared->mutex);
            munmap(mem, sizeof(SharedData));
            return 1;
        }
        if (pid == 0) worker(shared, i + 1);
        children[i] = pid;
    }

    bool ok = true;
    for (pid_t pid : children) {
        int status = 0;
        if (waitpid(pid, &status, 0) == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            ok = false;
    }
    std::cout << "Valoare finala: " << shared->counter << "\n";
    sem_destroy(&shared->mutex);
    munmap(mem, sizeof(SharedData));
    return ok ? 0 : 1;
}
