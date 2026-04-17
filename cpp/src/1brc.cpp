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
- Lets switch to std::span when we have a ptr and size together
- Implement custom hashmap with linear probing and transparent key lookup
- Add thread logic, split the file into N parts and have each thread process a part, then aggregate results at the end
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

/// Hash to use with transparent key lookup in unordered_map, allows for efficient lookup using stringview and only allocate key once
struct KeyHash
{
    using is_transparent = void;
    // Allows for string views, so we can lookup without allocating a new string
    size_t operator()(std::string_view key) const noexcept
    {
        return std::hash<std::string_view>{}(key);
    }
    // Allows for strings
    size_t operator()(const std::string &key) const noexcept
    {
        return std::hash<std::string_view>{}(key);
    }
};

/// @brief Represents the weather station found in "measurements.txt"
struct WeatherStation
{
    int_fast16_t min = 999;
    int_fast16_t max = -999;
    int_fast64_t total = 0;
    int count = 0; // Total instances of measurements we read
};

// Alias for the weather station map
using StationMap = std::unordered_map<std::string, WeatherStation, KeyHash, std::equal_to<>>;

/// @brief The return type for mmap, so we can cleanup memory after getting the data
struct MMapFile
{
    int fd = -1;
    size_t size = 0;
    const char *data = nullptr;

    // Default constructor, creates an empty MMapFile
    MMapFile() = default;

    // Constructor to initialize the MMapFile with given values
    MMapFile(int fd_in, size_t size_in, const char *data_in)
        : fd(fd_in), size(size_in), data(data_in)
    {
    }

    // Destructor to clean up resources, automatically called when MMapFile goes out of scope
    ~MMapFile()
    {
        if (data != nullptr)
        {
            ::munmap(const_cast<char *>(data), size);
        }
        if (fd >= 0)
        {
            ::close(fd);
        }
    }

    // Delete copy constructor and copy assignment operator to prevent copying of MMapFile instances, since they manage resources that should not be duplicated
    MMapFile(const MMapFile &) = delete;            // Dont allow the copy constructor so MmapFile a = b is not allowed
    MMapFile &operator=(const MMapFile &) = delete; // Dont allow the copy assignment operator so MmapFile a; a = b is not allowed

    // Allowing us to move MMapFile instances, transferring ownership of the resources without copying
    MMapFile(MMapFile &&other) noexcept
        : fd(other.fd), size(other.size), data(other.data)
    {
        other.fd = -1;
        other.size = 0;
        other.data = nullptr;
    }

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
        if (fd >= 0)
        {
            ::close(fd);
        }

        fd = other.fd;
        size = other.size;
        data = other.data;

        other.fd = -1;
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

    auto t3 = std::chrono::high_resolution_clock::now();
    std::println("mmap() time: {:.6f} seconds",
                 std::chrono::duration<double>(t3 - t2).count());

    // TODO: Switch to std span
    // Return mmap struct
    return {fd, size, static_cast<const char *>(ptr)};
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

void add_station(std::string_view name, int_fast16_t value, StationMap &weather_stations)
{
    auto it = weather_stations.find(name);
    if (it == weather_stations.end())
    {
        it = weather_stations.emplace(std::string(name), WeatherStation{}).first;
    }

    auto &ws = it->second;

    if (value < ws.min)
        ws.min = value;
    else if (value > ws.max)
        ws.max = value;
    ws.total += value;
    ws.count++;
}

StationMap create_weather_station_map(MMapFile &mapped)
{
    // Reserve 10000 slots for our unique stations
    StationMap weather_stations{};
    weather_stations.reserve(10000);

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
void output_stations(const StationMap &map)
{
    /// Collect and sort keys
    std::vector<std::string> keys;
    keys.reserve(map.size());
    for (auto &[key, _] : map)
        keys.push_back(key);
    std::sort(keys.begin(), keys.end());

    // Output
    std::print("{{");
    for (size_t i = 0; i < keys.size(); ++i)
    {
        // TODO: Fix this?
        auto &ws = map.at(keys[i]);
        int64_t sum = ws.total;
        if (sum > 0)
            sum += ws.count / 2; // rounding
        else
            sum -= ws.count / 2;
        double mean = static_cast<double>(sum / ws.count);

        // Print to 1 decimal place
        std::print("{}={:.1f}/{:.1f}/{:.1f}",
                   keys[i],
                   static_cast<double>(ws.min) / 10.0,
                   mean / 10.0,
                   static_cast<double>(ws.max) / 10.0);

        // Print comma separator if not the last element
        if (i + 1 < keys.size())
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
    StationMap weather_stations = create_weather_station_map(mapped);

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