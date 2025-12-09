#include <mpi.h>
#include <iostream>
#include <cstring>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <chrono>
#include <iomanip>
#include <string>

using namespace std;

const char Dictionary[62] =
{
    '0','1','2','3','4','5','6',
    '7','8','9','a','b','c','d',
    'e','f','g','h','i','j','k',
    'l','m','n','o','p','q','r',
    's','t','u','v','w','x','y',
    'z','A','B','C','D','E','F',
    'G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T',
    'U','V','W','X','Y','Z'
};

char Randomizer() {
    return Dictionary[rand() % 62];
}

size_t hashTransformation(const string &password){
    hash<string> hasher;
    return hasher(password);
}

string GenerateOriginalPasswordRandom(int length){
    string pass;
    pass.reserve(length);
    for (int i = 0; i < length; ++i) pass.push_back(Randomizer());
    return pass;
}

string GenerateOriginalPasswordLast(int length){
    return string(length, Dictionary[61]);
}

// Convierte un número (base-62) a contraseña
void indexToPassword(unsigned long long index, int length, string &pass){
    for (int i = 0; i < length; i++){
        pass[length - 1 - i] = Dictionary[index % 62];
        index /= 62;
    }
}

// Cálculo total del espacio de búsqueda: 62^length
unsigned long long totalSpace(int length){
    unsigned long long t = 1;
    for (int i = 0; i < length; i++) t *= 62ULL;
    return t;
}

int main(int argc, char* argv[]) {

    MPI_Init(&argc, &argv);

    int world_rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    srand(time(NULL) + world_rank);

    int length = 4;
    if (argc > 1) length = atoi(argv[1]);
    if (length < 1) length = 1;

    int N = 100;
    if (argc > 2) N = atoi(argv[2]);
    if (N < 1) N = 1;

    const char* outname = "results_mpi.csv";
    if (argc > 3) outname = argv[3];

    string mode = "rand";
    if (argc > 4) mode = argv[4];

    ofstream csv;
    if (world_rank == 0) {
        csv.open(outname);
        csv << fixed << setprecision(9);
    }

    unsigned long long space = totalSpace(length);
    string pass(length, Dictionary[0]);

    for (int it = 1; it <= N; ++it) {

        string original_pass;
        if (mode == "last") original_pass = GenerateOriginalPasswordLast(length);
        else original_pass = GenerateOriginalPasswordRandom(length);

        size_t target_hash = hashTransformation(original_pass);

        auto t0 = chrono::steady_clock::now();

        bool local_found = false;
        unsigned long long local_attempts = 0;
        string local_found_pass(length, Dictionary[0]);

        // Cada proceso prueba índices: rank, rank+size, rank+2*size, ...
        for (unsigned long long idx = world_rank; idx < space; idx += world_size) {

            indexToPassword(idx, length, pass);

            local_attempts++;

            if (hashTransformation(pass) == target_hash) {
                local_found = true;
                local_found_pass = pass;
                break;
            }
        }

        // Saber si algún proceso encontró la contraseña
        int global_found = 0;
        MPI_Allreduce(&local_found, &global_found, 1, MPI_INT, MPI_LOR, MPI_COMM_WORLD);

        // Reunir el número mínimo de intentos del proceso ganador
        unsigned long long global_attempts = 0;
        MPI_Reduce(&local_attempts, &global_attempts, 1, MPI_UNSIGNED_LONG_LONG,
                   MPI_MIN, 0, MPI_COMM_WORLD);

        // Recolectar la contraseña correcta del proceso que la encontró
        if (world_rank == 0) {
            char recvbuf[128];
            MPI_Status st;

            for (int p = 0; p < world_size; p++) {
                if (p == world_rank) continue;
                MPI_Recv(recvbuf, 128, MPI_CHAR, p, 1, MPI_COMM_WORLD, &st);
            }
        } else {
            if (local_found) {
                MPI_Send(local_found_pass.c_str(), local_found_pass.size() + 1,
                         MPI_CHAR, 0, 1, MPI_COMM_WORLD);
            } else {
                MPI_Send("", 1, MPI_CHAR, 0, 1, MPI_COMM_WORLD);
            }
        }

        auto t1 = chrono::steady_clock::now();
        long long nanos = chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();
        double time_s = nanos / 1e9;

        if (world_rank == 0) {
            csv << time_s << "\n";

            cout << "it=" << it
                 << " len=" << length
                 << " attempts=" << global_attempts
                 << " time(s)=" << fixed << setprecision(6) << time_s
                 << " found=" << original_pass
                 << "\n";
        }
    }

    if (world_rank == 0) csv.close();

    MPI_Finalize();
    return 0;
}
