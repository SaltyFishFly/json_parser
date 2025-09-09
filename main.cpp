#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

#include "json.hpp"
#include "json_parser.h"

static void test_speed()
{
    std::fstream file("large_test.json");
    std::string str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    auto begin1 = std::chrono::steady_clock::now();

    json::json_reader reader(str);
    auto my_res = reader.parse().value();

    auto end1 = std::chrono::steady_clock::now();
    std::cout << std::format("MyJson parsing took {} ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(end1 - begin1).count());

    auto begin2 = std::chrono::steady_clock::now();

    auto nlm_res = nlohmann::json::parse(str);

    auto end2 = std::chrono::steady_clock::now();
    std::cout << std::format("NlohmannJson parsing took {} ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(end2 - begin2).count());
}

static void test_cow_string()
{
    json::cow_string str1("Hello, World!");
    json::cow_string str2 = str1; // Shared ownership
    std::cout << "Before modification:\n";
    std::cout << "str1: " << str1.c_str() << "\n";
    std::cout << "str2: " << str2.c_str() << "\n";

    str2[7] = 'C'; // Modify str2, should trigger copy-on-write
    std::cout << "After modification 1:\n";
    std::cout << "str1: " << str1.c_str() << "\n"; // Should remain unchanged
    std::cout << "str2: " << str2.c_str() << "\n"; // Should reflect the change

    str2[7] = 'D'; // Modify str2, should trigger copy-on-write
    std::cout << "After modification 2:\n";
    std::cout << "str1: " << str1.c_str() << "\n"; // Should remain unchanged
    std::cout << "str2: " << str2.c_str() << "\n"; // Should reflect the change
}

static void test_parser()
{
    try
    {
        std::ios::sync_with_stdio(false);

        std::fstream file("large_test.json");
        std::string str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        json::json_reader reader(str);
        auto&& my_res = reader.parse();

        json::json_writer writer(std::cout);
        writer.write(my_res.value());
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
    }
}

int main()
{
    test_parser();
}