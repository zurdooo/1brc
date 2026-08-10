/*
This file should output results to stdout, we can pipe results to a file and compare with the existing solution


Nmap the file into memory
Split by available cores,
Aggregate results after
The task is to write a Java program which reads the file,
calculates the min, mean, and max temperature value per weather station,
and emits the results on stdout like this (i.e. sorted alphabetically by
station name, and the result values per station in the format <min>/<mean>/<max>,
rounded to one fractional digit):

Have a sorted hashmap implementation so we can quickly input,
aggregate, and retrieve the results per station.

! Test Input std::string name;double value
access value by name if not present, append to total, add 1 to struct count

TODO LIST
- MAYBE: Implement a better parsing function
- Remove atomic counter
- Hash while we parse line
*/

#include <print>
#include <unordered_map> // Fallback if Boost is not configured in your IDE/CMake
#include <string>
#include <vector>
#include <algorithm>
#include <string_view>
#include <charconv>
#include <cstdlib>
#include <cstdio>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <span>
#include <cstring>
#include <thread>

// ! Global Constants
unsigned int NUMBER_OF_THREADS = std::thread::hardware_concurrency();
size_t number_of_chunks = NUMBER_OF_THREADS * 4;
unsigned int CHUNK_BATCH_SIZE = 4;

/// @brief Represents the weather station found in "measurements.txt"
struct WeatherStation
{
    std::string_view key;

    int_fast16_t min;
    int_fast16_t max;
    int_fast32_t total;
    uint_fast32_t count; // Total instances of measurements we read

    // Init the key into struct, plus defaults
    void init(std::string_view key)
    {
        this->key = key;

        min = 999;
        max = -999;
        total = 0;
        count = 0;
    }

    //  Char by char comparison
    bool equals(std::string_view other_key) const
    {
        return key == other_key;
    }
};

// FNV-1a 32-bit hash function
// Generate a deterministic integer hash of a byte seq
static inline uint32_t FNVmanhash(std::string_view key)
{
    uint32_t hash_number = 2166136261u; // FNV Hash number, good for distributions and stuff
    size_t key_len = key.length();
    for (size_t i = 0; i < key_len; i++)
    {
        hash_number ^= (uint8_t)key[i]; // Update byte into number-state
        hash_number *= 16777619;        // Mix bits via multiplication (FNV prime) for avalanche effect
    }
    return hash_number;
}

// TODO: Increment hashsize
// TODO: Instead of two arrays move to 1,
/// @brief Custom hashmap implementation, linear probing, non dynamic resizing
struct HashMan
{
    WeatherStation *stations; // Stores the weather station
    uint_fast16_t *hashes;    // Stores the hash of a station key

    uint_fast16_t size;
    uint_fast16_t capacity;
    uint_fast16_t mask; // Bitmasking instead of modulo operation

    // Default Constructor
    HashMan()
    {
        capacity = 16384; // WE KNOW THE CAPACITY is 10,000 beforehand so we can go to the nearest power of 2 greater than 10,000, 2 ^ 14
        mask = capacity - 1;
        size = 0;

        // * Malloc enough slots for capacity number of WeatherStation elements
        stations = new WeatherStation[capacity]();
        hashes = new uint_fast16_t[capacity]();
    }

    // Destructor
    ~HashMan()
    {
        delete[] stations;
        delete[] hashes;
    }

    // Move Constructor
    HashMan(HashMan &&other) noexcept : stations(other.stations), hashes(other.hashes), size(other.size), capacity(other.capacity), mask(other.mask)
    {
        other.stations = nullptr;
        other.hashes = nullptr;
        other.size = 0;
        other.capacity = 0;
        other.mask = 0;
    }

    // * Disables copying
    HashMan(const HashMan &) = delete;
    HashMan &operator=(const HashMan &) = delete;

    // TODO: Quadratic Probing approach?
    // TODO: Edit the station when we have a match
    // Looks for value in table, linear probing approach
    WeatherStation *get_or_create(std::string_view key)
    {
        // Shift by 1, so 0 can remain the empty slot flag
        // uint_fast16_t key_hash = FNVmanhash(key) + 1;
        uint_fast16_t key_hash = std::hash<std::string_view>{}(key) + 1;
        uint_fast16_t idx = key_hash & mask; // Simulate modulo of capacity but with 1 less than a power of 2 is faster

        // Probe for empty slot
        while (true)
        {
            // Empty Insert
            if (hashes[idx] == 0)
            {
                size++;
                hashes[idx] = key_hash;
                // Initialize entry
                stations[idx].init(key);

                return &stations[idx];
            }
            // Potential match
            else if (hashes[idx] == key_hash && stations[idx].equals(key))
            {
                return &stations[idx];
            }

            // Otherwise linear probe forward
            idx = (idx + 1) & mask;
        }
    }

    // Retrieves the value, immutable
    const WeatherStation *get(std::string_view key) const
    {
        // Shift by 1, so 0 can remain the empty slot flag
        // uint_fast16_t key_hash = FNVmanhash(key) + 1;
        uint_fast16_t key_hash = std::hash<std::string_view>{}(key) + 1;
        uint_fast16_t idx = key_hash & mask;

        while (true)
        {
            if (hashes[idx] == key_hash && stations[idx].equals(key))
            {
                return &stations[idx];
            }
            idx = (idx + 1) & mask;
        }
        return &stations[idx];
    }
};

/// @brief The return type for mmap, so we can cleanup memory after getting the data
struct MMapFile
{
    size_t size = 0;
    const char *data = nullptr;

    // Default constructor, creates an empty MMapFile
    MMapFile() = default;

    // Constructor to initialize the MMapFile with given values
    MMapFile(size_t size_in, const char *data_in)
        : size(size_in), data(data_in)
    {
    }

    // Destructor to clean up resources, automatically called when MMapFile goes out of scope
    ~MMapFile()
    {
        if (data != nullptr)
        {
            ::munmap(const_cast<char *>(data), size);
        }
    }

    // Delete copy constructor and copy assignment operator to prevent copying of MMapFile instances, since they manage resources that should not be duplicated
    MMapFile(const MMapFile &) = delete;            // Dont allow the copy constructor so MmapFile a = b is not allowed
    MMapFile &operator=(const MMapFile &) = delete; // Dont allow the copy assignment operator so MmapFile a; a = b is not allowed

    // * Move Constructor
    // Allowing us to move MMapFile instances, transferring ownership of the resources without copying
    MMapFile(MMapFile &&other) noexcept
        : size(other.size), data(other.data)
    {
        other.size = 0;
        other.data = nullptr;
    }

    // * Move assignment
    // Move assignment operator to transfer ownership of resources from one MMapFile instance to another, ensuring proper cleanup of existing resources before taking ownership of the new ones
    // Make sure we have unique ownership
    MMapFile &operator=(MMapFile &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        if (data != nullptr)
        {
            ::munmap(const_cast<char *>(data), size);
        }
        size = other.size;
        data = other.data;

        other.size = 0;
        other.data = nullptr;
        return *this;
    }

    // Function to create an vector of chunks so that we can distribute them to the threads
    // Chunk mmap file into four times more than number of threads, play around with this number
    // Make sure we get a starting and ending byte for every chunk
    std::vector<std::span<const char>> chunkify()
    {

        size_t chunk_size = size / number_of_chunks;
        const char *chunk_begin = data;
        const char *file_end = data + size;

        std::vector<std::span<const char>> chunks{};
        // -1 in the chunk number since we set the last one manually
        for (size_t i = 0; i < number_of_chunks - 1 && chunk_begin < file_end; i++)
        {
            const char *chunk_end = chunk_begin + chunk_size;
            if (chunk_end > file_end)
            {
                chunk_end = file_end;
            }
            // Find end char
            while (chunk_end != file_end && *chunk_end != '\n')
            {
                chunk_end++;
            }
            // Move to start of new line as range is not inclusive
            if (chunk_end != file_end)
            {
                chunk_end++;
            }
            chunks.push_back({chunk_begin, chunk_end});
            // Restart chunk start pointer
            chunk_begin = chunk_end;
        }

        // Push last chunk
        if (chunk_begin < file_end)
        {
            chunks.push_back({chunk_begin, file_end});
        }
        return chunks;
    }
};

/// @brief mmaps "measurements.txt" and gets a pointer to the data
/// @return Pointer to the mapped data
MMapFile mmap_file()
{
    // Parse file into map
    const char *path = "../measurements.txt";

    // Open file
    int fd = ::open(path, O_RDONLY);
    if (fd < 0)
    {
        std::perror("open");
        return {};
    }

    // * Kernel advise
#if defined(POSIX_FADV_SEQUENTIAL)
    ::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#elif defined(F_RDAHEAD)
    ::fcntl(fd, F_RDAHEAD, 1);
#endif

    // Get file size
    struct stat st{};
    if (::fstat(fd, &st) != 0)
    {
        std::perror("fstat");
        ::close(fd);
        return {};
    }

    const size_t size = static_cast<size_t>(st.st_size);
    if (size == 0)
    {
        ::close(fd);
        return {};
    }

    // Call mmap and get the pointer to the data
    void *ptr = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (ptr == MAP_FAILED)
    {
        std::perror("mmap");
        ::close(fd);
        return {};
    }
    ::close(fd);

    // * More kernel advise
    ::madvise(ptr, size, MADV_SEQUENTIAL);
#if defined(MADV_HUGEPAGE)
    ::madvise(ptr, size, MADV_HUGEPAGE);
#endif

    // Return mmap struct
    return {size, static_cast<const char *>(ptr)};
}

// TODO: branch on length
/// @brief Parses the temperature value from a semicolon pointer and returns scaled int
int_fast16_t parse_value(const char *sc)
{
    const char *p = sc + 1; // skip semicolon

    // Check for neg
    bool neg = false;
    if (*p == '-')
    {
        neg = true;
        ++p; // skip '-'
    }

    int value = 0;

    while (*p != '.')
    {
        value = value * 10 + (*p - '0');
        ++p;
    }

    ++p;                             // skip '.'
    value = value * 10 + (*p - '0'); // parse fractional digit

    int_fast16_t result = value;
    return neg ? -result : result;
}

/// @brief Extracts the station name view from line start to semicolon pointer
std::string_view parse_station(const char *line_start, const char *sc)
{
    return {line_start, static_cast<size_t>(sc - line_start)};
}

// TODO: Compute hash while we iterate, so we dont parse over string more than once, use java solution approach
/// @brief Parses a line using pointer arithmetic and advances iter to the next line
/// @param iter The pointer pointing to the current position in the data, passed by reference and updated during parsing
/// @param out_name Output parameter for the parsed station name, passed by reference and set during execution
/// @param out_value Output parameter for the parsed temperature value, passed by reference and set during execution
void parse_line(const char *&iter, std::string_view &out_name, int_fast16_t &out_value)
{
    const char *line_start = iter;
    const char *p = iter;
    const char *sc = nullptr;

    while (true)
    {
        if (*p == ';')
        {
            sc = p;
            p += 3; // after ';' there is always at least 3 chars
            continue;
        }

        if (*p == '\n')
        {
            // Move iter to the start of the next line, which is after the newline character
            iter = p + 1;
            break;
        }

        ++p;
    }

    // Extract station name and value
    out_name = parse_station(line_start, sc);
    out_value = parse_value(sc);
}

/// @brief Adds station to map and updates its data with value
void add_station(std::string_view name, int_fast16_t value, HashMan &weather_stations)
{
    auto station_ptr = weather_stations.get_or_create(name);

    // Grab reference from pointer
    auto &station = *station_ptr;

    // TODO: move to weather station method
    if (value < station.min)
        station.min = value;
    if (value > station.max)
        station.max = value;

    station.total += value;
    station.count++;
}

/// @brief
/// @param local_map, reference to this threads own map which it will fill
/// @param current_chunk, reference to the atomic index counter that all threads increment, take a batch of chunks, for now lets say 4
/// @param chunks, reference to the chunks array, we cant modify it as its shared we only read
void multi_thread_fill_weather_station_map(HashMan &local_map, std::atomic<size_t> &atomic_chunk, const std::vector<std::span<const char>> &chunks)
{
    // Grab the index of chunks to work on
    while (true)
    {
        size_t current_chunk_start = atomic_chunk.fetch_add(CHUNK_BATCH_SIZE);
        if (current_chunk_start >= chunks.size())
        {
            break;
        }
        size_t current_chunk_end = std::min(current_chunk_start + CHUNK_BATCH_SIZE, chunks.size());

        // Variables we will use to store the parsed station name and value, passed by reference to the parsing function
        std::string_view name;
        int_fast16_t value;
        // Process individual chunk
        for (size_t i = current_chunk_start; i < current_chunk_end; i++)
        {
            auto curr_chunk = chunks[i];

            const char *it = curr_chunk.data();
            const char *end = curr_chunk.data() + curr_chunk.size();

            while (it != end)
            {
                // Tell the CPU to start fetching ~64 bytes ahead right now,
                // don't wait until we actually need it
                __builtin_prefetch(it + 512, 0, 0);
                parse_line(it, name, value);
                add_station(name, value, local_map);
            }
        }
    }
}

/// This function should take in the hashmap of all stations and output in the desired format to stdout
void output_stations(const HashMan &map)
{
    /// Collect and sort indices
    std::vector<uint_fast16_t> indices;
    indices.reserve(map.size);
    for (uint_fast16_t idx = 0; idx < map.capacity; ++idx)
    {
        // We have a hit
        if (map.hashes[idx] != 0)
        {
            indices.push_back(idx);
        }
    }

    // Comparator as we store indices not the keys
    std::sort(indices.begin(), indices.end(), [&](uint_fast16_t a, uint_fast16_t b)
              { return map.stations[a].key < map.stations[b].key; });

    // Output
    std::print("{{");
    for (size_t i = 0; i < indices.size(); ++i)
    {
        const WeatherStation &ws = map.stations[indices[i]];

        int64_t sum = ws.total;
        if (sum > 0)
            sum += ws.count / 2; // rounding
        else
            sum -= ws.count / 2;
        double mean = static_cast<double>(sum) / static_cast<double>(ws.count);

        // Print to 1 decimal place
        std::print("{}={:.1f}/{:.1f}/{:.1f}",
                   ws.key,
                   static_cast<double>(ws.min) / 10.0,
                   mean / 10.0,
                   static_cast<double>(ws.max) / 10.0);

        // Print comma separator if not the last element
        if (i + 1 < indices.size())
            std::print(", ");
    }
    // Print closing brace, }}
    std::println("}}");
}

void read_every_char_in_given_bytes(const char *mmap_start, uint_fast64_t memory_size, uint thread_id)
{
    volatile uint_fast64_t sink = 0;
    const char *ptr = mmap_start + (thread_id * memory_size);
    for (uint_fast64_t i = 0; i < memory_size; i++, ptr++)
    {
        // if (*ptr == ';' || *ptr == '\n')
        //     sink += *ptr;
        sink += 1;
    }
}

void one_br_perf()
{
    auto start_time = std::chrono::high_resolution_clock::now();

    // Mmap file and get pointer to data
    // Read file into memory
    MMapFile mapped = mmap_file();

    std::vector<std::thread> threads;
    uint_fast64_t thread_memory_size = mapped.size / NUMBER_OF_THREADS;

    for (uint i = 0; i < NUMBER_OF_THREADS; i++)
    {
        threads.emplace_back(
            read_every_char_in_given_bytes, std::ref(mapped.data), thread_memory_size, i);
    }

    for (auto &t : threads)
    {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> time_elapse = end_time - start_time;
    std::println("The perf thread phase finished at seconds (duration: {:.6f} seconds)",
                 time_elapse.count());
}

static constexpr uint64_t SEMICOLON_PATTERN = 0x3B3B3B3B3B3B3B3BULL;
static constexpr uint64_t NEWLINE_PATTERN   = 0x0A0A0A0A0A0A0A0AULL;

static inline uint64_t match_byte(uint64_t word, uint64_t pattern) {
    uint64_t m = word ^ pattern;
    return (m - 0x0101010101010101ULL) & (~m & 0x8080808080808080ULL);
}

void read_every_char_in_given_bytes_2(const char *mmap_start, uint_fast64_t memory_size, uint thread_id,
                                    std::atomic<uint64_t> &total_sc, std::atomic<uint64_t> &total_nl)
{
    uint64_t semicolons = 0, newlines = 0;

    const char *ptr   = mmap_start + (thread_id * memory_size);
    const char *end   = ptr + memory_size;
    const char *end8  = ptr + (memory_size & ~7ULL);

    for (; ptr < end8; ptr += 8) {
        uint64_t word;
        std::memcpy(&word, ptr, 8);
        semicolons += __builtin_popcountll(match_byte(word, SEMICOLON_PATTERN));
        newlines   += __builtin_popcountll(match_byte(word, NEWLINE_PATTERN));
    }

    for (; ptr < end; ptr++) {
        semicolons += (*ptr == ';');
        newlines   += (*ptr == '\n');
    }

    total_sc.fetch_add(semicolons, std::memory_order_relaxed);
    total_nl.fetch_add(newlines,   std::memory_order_relaxed);
}

void one_br_perf_2()
{
    auto start_time = std::chrono::high_resolution_clock::now();

    MMapFile mapped = mmap_file();

    std::atomic<uint64_t> total_sc{0}, total_nl{0};
    std::vector<std::thread> threads;
    uint_fast64_t thread_memory_size = mapped.size / NUMBER_OF_THREADS;

    for (uint i = 0; i < NUMBER_OF_THREADS; i++)
    {
        threads.emplace_back(
            read_every_char_in_given_bytes_2, mapped.data, thread_memory_size, i,
            std::ref(total_sc), std::ref(total_nl));
    }

    for (auto &t : threads)
        t.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> time_elapse = end_time - start_time;
    std::println("semicolons: {}  newlines: {}", total_sc.load(), total_nl.load());
    std::println("The perf thread phase finished at seconds (duration: {:.6f} seconds)",
                 time_elapse.count());
}

int main()
{
    one_br_perf_2();
    std::println("Perf done");

    auto start_time = std::chrono::high_resolution_clock::now();

    // Mmap file and get pointer to data
    // Read file into memory
    MMapFile mapped = mmap_file();
    if (mapped.data == nullptr || mapped.size == 0)
    {
        return 1;
    }

    std::vector<std::span<const char>>
        chunks = mapped.chunkify();

    // Atomic indexer for chunks
    std::atomic<size_t> atomic_chunk_counter = 0;

    // All hashmaps
    std::vector<HashMan> all_hashes(NUMBER_OF_THREADS);

    // Container for threads
    std::vector<std::thread> threads;
    threads.reserve(NUMBER_OF_THREADS); // * Reserve dont spawn defaults since we want to fill threads with worker function call back

    // Spawn threads with their own maps
    auto thread_phase_start = std::chrono::high_resolution_clock::now();
    for (uint i = 0; i < NUMBER_OF_THREADS; i++)
    {
        // Spawn thread in container
        threads.emplace_back(multi_thread_fill_weather_station_map, std::ref(all_hashes[i]), std::ref(atomic_chunk_counter), std::ref(chunks));
    }

    // Await for work to finish
    for (auto &t : threads)
    {
        t.join();
    }

    auto thread_phase_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> thread_phase_elapsed = thread_phase_end - thread_phase_start;
    std::chrono::duration<double> since_start_to_thread_finish = thread_phase_end - start_time;
    std::println(stderr, "Thread phase finished at {:.6f} seconds (duration: {:.6f} seconds)",
                 since_start_to_thread_finish.count(), thread_phase_elapsed.count());

    // TODO: In future look to faster merging strategies
    // Chose first local map as global
    HashMan &merged_map = all_hashes[0];
    auto merge_phase_start = std::chrono::high_resolution_clock::now();
    for (size_t i = 1; i < all_hashes.size(); i++)
    {
        HashMan &current_hash_map = all_hashes[i];
        // Walk through all indices in this hashmap
        for (uint_fast16_t idx = 0; idx < current_hash_map.capacity; ++idx)
        {
            if (current_hash_map.hashes[idx] != 0)
            {
                WeatherStation &current_station = current_hash_map.stations[idx];

                WeatherStation &merged_station = *merged_map.get_or_create(current_station.key);

                // Update global station with current station val
                if (current_station.min < merged_station.min)
                    merged_station.min = current_station.min;
                if (current_station.max > merged_station.max)
                    merged_station.max = current_station.max;

                merged_station.total += current_station.total;
                merged_station.count += current_station.count;
            }
        }
    }

    auto merge_phase_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> merge_phase_elapsed = merge_phase_end - merge_phase_start;
    std::chrono::duration<double> since_start_to_merge_finish = merge_phase_end - start_time;
    std::println(stderr, "Merge phase finished at {:.6f} seconds (duration: {:.6f} seconds)",
                 since_start_to_merge_finish.count(), merge_phase_elapsed.count());

    output_stations(merged_map);

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    std::println("Elapsed since start: {:.6f} seconds", elapsed.count());

    return 0;
}
