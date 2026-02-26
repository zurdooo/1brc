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
*/

#include <print>
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>
#include <string_view>
#include <charconv>
#include <limits>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>


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
    double min = std::numeric_limits<double>::infinity();
    double max = -std::numeric_limits<double>::infinity();
    double total = 0.0;
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

    // ! Advise the kernel about the expected access pattern
    ::madvise(ptr, size, MADV_SEQUENTIAL);

    // Return mmap struct
    return {fd, size, static_cast<const char *>(ptr)};
}

// TODO: we can search after the ; by looking for a . as we know how many chars are left at this point
void find_sep_and_new_line(const char *line_start, size_t remaining, const char *&sc, const char *&nl)
{
    const char *p = line_start;
    const char *end = line_start + remaining;
    sc = nullptr;
    nl = nullptr;

    p += 2; // ?
    while (p < end)
    {
        if (*p == ';')
        {
            sc = p;
            p += 3; // ?
            continue;
        }
        else if (*p == '\n')
        {
            nl = p;
            break;
        }
        ++p;
    }
}

double parse_value(const char *sc)
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

    double result = value * 0.1;
    return neg ? -result : result;
}

// TODO: Change to another hashmap instead of naive cpp impl
void aggregate(const char *data, size_t pos, const char *sc, double value, StationMap &weather_stations)
{
    std::string_view name_view{data + pos, static_cast<size_t>(sc - (data + pos))};

    auto it = weather_stations.find(name_view);
    if (it == weather_stations.end())
    {
        it = weather_stations.emplace(std::string(name_view), WeatherStation{}).first;
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

StationMap create_weather_station_map()
{
    auto start = std::chrono::high_resolution_clock::now();

    // Read file into memory
    MMapFile mapped = mmap_file();
    if (mapped.data == nullptr || mapped.size == 0)
    {
        return {};
    }
    const char *data = mapped.data;
    const size_t size = mapped.size;

    auto read_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> read_time = read_end - start;
    std::println("File read time: {:.6f} seconds", read_time.count());

    StationMap weather_stations{};
    weather_stations.reserve(10000);

    const char *nl = nullptr;
    const char *sc = nullptr;

    // Read through the data
    size_t pos = 0;

    // TEMP: Added this to limit the iterations
    int row_count = 0;
    const int MAX_ROWS = 20000000;

    // TEMP: Removed limit
    while (pos < size)
    {
        find_sep_and_new_line(data + pos, size - pos, sc, nl);

        // Number is contained after semicolon
        double value = parse_value(sc);

        aggregate(data, pos, sc, value, weather_stations);

        if (!nl)
            break; // no newline, done

        // Update to next line
        pos = (nl - data) + 1;

        row_count++;
    }

    ::munmap(const_cast<char *>(data), size);
    ::close(mapped.fd);

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
        auto &ws = map.at(keys[i]);
        double mean = ws.total / ws.count;

        // Print to 1 decimal place
        std::print("{}={:.1f}/{:.1f}/{:.1f}",
                   keys[i],
                   ws.min,
                   mean,
                   ws.max);

        // Print comma separator if not the last element
        if (i + 1 < keys.size())
            std::print(", ");
    }
    // Print closing brace, }}
    std::println("}}");
}

int main()
{
    std::println("Starting 1brc program");

    auto start = std::chrono::high_resolution_clock::now();

    // Parse file into sorted key hashmap
    StationMap weather_stations = create_weather_station_map();

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