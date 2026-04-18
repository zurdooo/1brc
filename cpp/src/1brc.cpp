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
- Add thread logic, split the file into N parts and have each thread process a part, then aggregate results at the end
    - For threads go with an atomic iterator that goes through threads, anytime this atomic var gets called, it feeds a chunk
    - To the requesting thread
- MAYBE: Implement a better parsing function
*/

#include <print>
#include <unordered_map> // Fallback if Boost is not configured in your IDE/CMake
#include <string>
#include <vector>
#include <algorithm>
#include <string_view>
#include <charconv>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <span>

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
static inline uint_fast32_t FNVmanhash(std::string_view key)
{
    uint_fast32_t hash_number = 2166136261u; // FNV Hash number, good for distributions and stuff
    size_t key_len = key.length();
    for (size_t i = 0; i < key_len; i++)
    {
        hash_number ^= (uint8_t)key[i]; // Update byte into number-state
        hash_number *= 16777619;        // Mix bits via multiplication (FNV prime) for avalanche effect
    }
    return hash_number;
}

// TODO: Add a move function
// TODO: Add a const non mutating find
struct HashMan
{
    WeatherStation *stations;
    uint_fast32_t *hashes; // Stores the hash of a station key

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
        hashes = new uint_fast32_t[capacity]();
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
    // TODO: Fix control flow in potential match
    // Looks for value in table, linear probing approach
    WeatherStation *get_or_create(std::string_view key)
    {
        // Shift by 1, so 0 can remain the empty slot flag
        uint_fast32_t key_hash = FNVmanhash(key) + 1;
        uint_fast32_t idx = key_hash & mask;

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
        uint_fast32_t key_hash = FNVmanhash(key) + 1;
        uint_fast32_t idx = key_hash & mask;

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
};

/// @brief mmaps "measurements.txt" and gets a pointer to the data
/// @return Pointer to the mapped data
MMapFile mmap_file()
{
    auto t0 = std::chrono::high_resolution_clock::now();

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
    ::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);

    auto t1 = std::chrono::high_resolution_clock::now();
    std::println("open() time: {:.6f} seconds",
                 std::chrono::duration<double>(t1 - t0).count());

    // Get file size
    struct stat st{};
    if (::fstat(fd, &st) != 0)
    {
        std::perror("fstat");
        ::close(fd);
        return {};
    }

    auto t2 = std::chrono::high_resolution_clock::now();
    std::println("fstat() time: {:.6f} seconds",
                 std::chrono::duration<double>(t2 - t1).count());

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

    auto t3 = std::chrono::high_resolution_clock::now();
    std::println("mmap() time: {:.6f} seconds",
                 std::chrono::duration<double>(t3 - t2).count());

    // Return mmap struct
    return {size, static_cast<const char *>(ptr)};
}

// @brief Parses the temperature value from a semicolon pointer and returns scaled int
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

// @brief Extracts the station name view from line start to semicolon pointer
std::string_view parse_station(const char *line_start, const char *sc)
{
    return {line_start, static_cast<size_t>(sc - line_start)};
}

// TODO: Compute hash while we iterate, so we dont parse over string more than once
// @brief Parses a line using pointer arithmetic and advances iter to the next line
// @param iter The pointer pointing to the current position in the data, passed by reference and updated during parsing
// @param out_name Output parameter for the parsed station name, passed by reference and set during execution
// @param out_value Output parameter for the parsed temperature value, passed by reference and set during execution
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

// @brief Adds station to map and updates its data with value
void add_station(std::string_view name, int_fast16_t value, HashMan &weather_stations)
{
    auto station_ptr = weather_stations.get_or_create(name);

    // Grab reference from pointer
    auto &station = *station_ptr;

    // TODO: move to weather staion method
    if (value < station.min)
        station.min = value;
    else if (value > station.max)
        station.max = value;

    station.total += value;
    station.count++;
}

// @brief Parse and map creation loop
HashMan create_weather_station_map(MMapFile &mapped)
{
    HashMan weather_stations{};

    const char *iter = mapped.data;
    const char *end = mapped.data + mapped.size;

    int row_count = 0;
    int max_rows = 50000000; // TEMP: Added this to limit the iterations

    // Variables we will use to store the parsed station name and value, passed by reference to the parsing function
    std::string_view name;
    int_fast16_t value;

    while (iter < end)
    {
        parse_line(iter, name, value);

        add_station(name, value, weather_stations);

        // row_count++;
    }

    return weather_stations;
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

int main()
{
    auto start = std::chrono::high_resolution_clock::now();
    // Mmap file and get pointer to data
    std::println("Starting mmap");

    // Read file into memory
    MMapFile mapped = mmap_file();
    if (mapped.data == nullptr || mapped.size == 0)
    {
        return 1;
    }

    std::println("Ended mmap");
    auto read_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> read_time = read_end - start;
    std::println("File read time: {:.6f} seconds", read_time.count());

    // Parse file into sorted key hashmap
    HashMan weather_stations = create_weather_station_map(mapped);

    auto created_map = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> map_creation_time = created_map - start;
    std::println("Map creation/parsing time: {:.6f} seconds", map_creation_time.count());

    // Pass hashmap into output function
    output_stations(weather_stations);

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::println("Total elapsed time: {:.6f} seconds", elapsed.count());

    std::println("Finished 1brc program");
    return 0;
}
