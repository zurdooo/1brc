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
- Do i need such a complicated hash?
- Only 100000 unique stations how can we optimize from here
- Fix the parsing function use the simd instructions
- Use simple/small objects in datastructures only use what we need
- Think about the memory pattern optimize a single query then optimize the loop
*/

#include <cstdio>
#include <unordered_map> // Fallback if Boost is not configured in your IDE/CMake
// TODO: Uncomment once boost is available
// #include <boost/unordered/unordered_flat_map.hpp>
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

// TODO: Fix the hashing and storing of keys
/// Hash to use with transparent key lookup in unordered_map, allows for efficient lookup using stringview and only allocate key once
struct KeyHash
{
    using is_transparent = void;
    size_t operator()(std::string_view key) const noexcept
    {
        return std::hash<std::string_view>{}(key);
    }
    size_t operator()(const std::string &key) const noexcept
    {
        return std::hash<std::string_view>{}(key);
    }
};

// TODO: Change doubles maybe? change to ints since we only have 1 decimal place,
// TODO: treat 1.2 as 12 and then when outputting add a decimal
/// @brief Represents the weather station found in "measurements.txt"
struct WeatherStation
{
    int_fast32_t min = 999;
    int_fast32_t max = -999;
    double total = 0.0;
    int count = 0; // Total instances of measurements we read
};

// Alias for the weather station map — std::string keys so they own their data (safe after munmap)
using StationMap = std::unordered_map<std::string, WeatherStation, KeyHash, std::equal_to<>>;

/// @brief The return type for mmap, so we can cleanup memory after getting the data
struct MMapFile
{
    int fd = -1;
    size_t size = 0;
    const char *data = nullptr;
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
    printf("open() time: %.6f seconds\n",
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
    printf("fstat() time: %.6f seconds\n",
           std::chrono::duration<double>(t2 - t1).count());

    const size_t size = static_cast<size_t>(st.st_size);
    if (size == 0)
    {
        ::close(fd);
        return {};
    }

    // Call mmap and get the pointer to the data
    void *ptr = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);

    // Close file now that we have the data
    ::close(fd);

    if (ptr == MAP_FAILED)
    {
        std::perror("mmap");
        ::close(fd);
        return {};
    }

    auto t3 = std::chrono::high_resolution_clock::now();
    printf("mmap() time: %.6f seconds\n",
           std::chrono::duration<double>(t3 - t2).count());

    // Return mmap struct
    return {fd, size, static_cast<const char *>(ptr)};
}

/// @brief Parses the temperature value from a string
/// @param sc Pointer to the semicolon in the line
/// @return The parsed temperature value
int_fast32_t parse_value(const char *sc)
{
    const char *p = sc + 1; // skip semicolon

    // Check for neg
    bool neg = (*p == '-');
    if (neg)
        ++p;

    int value = 0;

    // Parse integer part
    while (*p != '.')
    {
        value = value * 10 + (*p - '0');
        ++p;
    }

    ++p; // skip '.'

    // Exactly one fractional digit
    value = value * 10 + (*p - '0');

    int_fast32_t result = value;
    return neg ? -result : result;
}

/// @brief Finds the semicolon in a line as well as the new line character
/// @brief Output variables are passed by reference, the sc and nl ptrs are returned
void parse_sc_nl(const char *line_start, const char *&out_sc, const char *&out_nl)
{

    const char *sc = nullptr;
    const char *p = line_start;

    // Every word is at least two chars long
    p += 2; // ?
    while (true)
    {
        if (*p == ';')
        {
            sc = p;
            out_sc = sc;
            // After semicolon theres always at least three chars
            p += 3;
            continue;
        }
        else if (*p == '\n')
        {
            out_nl = p;
            break;
        }
        ++p;
    }
}

/// @brief Adds a station to the weather stations map
void add_station(const char *line_start, const char *sc, StationMap &weather_stations)
{
    int_fast32_t value = parse_value(sc);
    std::string_view name{line_start, static_cast<size_t>(sc - line_start)};

    auto it = weather_stations.find(name);
    if (it == weather_stations.end())
    {
        it = weather_stations.emplace(std::string(name), WeatherStation{}).first;
    }

    auto &ws = it->second;

    if (value < ws.min)
    {
        ws.min = value;
    }

    if (value > ws.max)
    {
        ws.max = value;
    }

    ws.total += value;
    ws.count++;
}

// TODO: Fix the logic/variable names
/// @brief function to parse the file and create the hashmap with all the values,
/// @return Returns the hashmap
StationMap create_weather_station_map()
{
    printf("Starting mmap\n");
    auto start = std::chrono::high_resolution_clock::now();

    // Read file into memory
    MMapFile mapped = mmap_file();
    if (mapped.data == nullptr || mapped.size == 0)
    {
        return {};
    }

    printf("Ended mmap\n");

    const char *first_ptr = mapped.data;
    const size_t size = mapped.size;

    auto read_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> read_time = read_end - start;
    printf("File read time: %.6f seconds\n", read_time.count());

    // Reserve 10000 slots for our unique stations
    StationMap weather_stations{};
    weather_stations.reserve(10000);

    // pointers to the end of a line
    const char *nl = nullptr;
    const char *sc = nullptr;

    // pointer to our current position, will point to the start of the current line
    size_t pos = 0;

    int row_count = 0;
    // TEMP: Added this to limit the iterations
    const int MAX_ROWS = 10000000;

    // TEMP: Removed limit
    while (pos < size)
    {
        // Find value and add stations
        parse_sc_nl(first_ptr + pos, sc, nl);
        add_station(first_ptr + pos, sc, weather_stations);

        if (!nl)
            break; // no newline, done

        // Update to next line, first_ptr gives the memory offset,
        // add 1 to get past the new line char
        pos = (nl - first_ptr) + 1;

        row_count++;
    }

    ::munmap(const_cast<char *>(first_ptr), size);
    ::close(mapped.fd);

    return weather_stations;
}

// TODO: Add thread spawner

// TODO: Add thread coordinator, aggregate thread results

// TODO: use iterator instead of basic loop
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
    printf("{");
    for (size_t i = 0; i < keys.size(); ++i)
    {
        // TODO: Fix this?
        auto &ws = map.at(keys[i]);
        double mean = ws.total / ws.count;

        // Print to 1 decimal place
        printf("%s=%.1f/%.1f/%.1f",
                   keys[i].c_str(),
                   static_cast<double>(ws.min) / 10.0,
                   mean / 10.0,
                   static_cast<double>(ws.max) / 10.0);

        // Print comma separator if not the last element
        if (i + 1 < keys.size())
            printf(", ");
    }
    // Print closing brace, }}
    printf("}\n");
}

int main()
{
    printf("Starting 1brc program\n");

    auto start = std::chrono::high_resolution_clock::now();

    // Parse file into sorted key hashmap
    StationMap weather_stations = create_weather_station_map();

    auto created_map = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> map_creation_time = created_map - start;
    printf("Map creation/parsing time: %.6f seconds\n", map_creation_time.count());

    // Pass hashmap into output function
    output_stations(weather_stations);

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    printf("Total elapsed time: %.6f seconds\n", elapsed.count());

    printf("Finished 1brc program\n");
    return 0;
}