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
#include <map>
#include <string>

struct WeatherStationKey
{
    std::string name;
};

struct WeatherStation
{
    double min;
    double max;
    double total;
    int count; // Total instances of measurements we read
};

std::map<WeatherStationKey, WeatherStation> create_weather_station_map()
{
    // Read file into memory
    const char *path = "measurements.txt";

    std::map<WeatherStationKey, WeatherStation> weather_stations{};
    // Parse file into map

    return weather_stations;
}

/// This function should take in the hashmap of all stations and output in the desired format to stdout
void output_stations(std::map<WeatherStationKey, WeatherStation> map) {}

int main()
{
    std::println("Starting 1brc program");

    // Parse file into sorted key hashmap
    std::map<WeatherStationKey, WeatherStation> weather_stations = create_weather_station_map();

    // Pass hashmap into output function
    output_stations(weather_stations);

    std::println("Finished 1brc program");
    return 0;
}