#pragma once
#ifndef _JSON_PARSER_H
#define _JSON_PARSER_H

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <map>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// Author: FishFly
// Time: 2025/09/02
//
// 一个轻量级的 JSON 解析库，使用C++20编写
// 解析 17 万行的 JSON 文件 大约需要 60ms
// 速度约为 Nlohmann/json 的一半
// 使用 COW 字符串减少复制
// 练手作品

namespace json
{
    class cow_string
    {
    public:
        // 返回一个代理，用于拦截对单个字符的写操作
        // 如果字符串的状态为 shared 则先复制再写
        class char_proxy
        {
        public:
            char_proxy(cow_string& str, size_t index) noexcept :
                str(str), index(index)
            { }

            operator char() const noexcept
            {
                return str.storage[str.offset + index];
            }

            char_proxy& operator=(char c)
            {
                if (str.storage.use_count() > 1)
                    str.copy();
                str.storage[str.offset + index] = c;
                return *this;
            }

        private:
            cow_string& str;
            size_t index;
        };
        friend struct std::less<cow_string>;
        friend std::ostream& operator<<(std::ostream& os, const cow_string& str);

        static inline constexpr const size_t npos = std::string::npos;

        cow_string(std::string_view view)
        {
            storage = std::make_shared<char[]>(view.size() + 1);
            std::copy(view.begin(), view.end(), storage.get());
            length = view.size();
            offset = 0;
            storage[length] = '\0';
        }
        cow_string(const cow_string& other) = default;
        cow_string(cow_string&&) noexcept = default;
        cow_string& operator=(const cow_string&) noexcept = default;
        cow_string& operator=(cow_string&&) noexcept = default;

        // operators
        explicit operator std::string_view() const noexcept
        {
            return std::string_view(storage.get() + offset, length);
        }

        char_proxy operator[](size_t index) noexcept
        {
            return char_proxy{ *this, index };
        }

        char operator[](size_t index) const noexcept
        {
            return storage[offset + index];
        }

        bool operator==(const char* other) const noexcept
        {
            size_t other_length = std::char_traits<char>::length(other);
            if (length != other_length)
                return false;
            return std::equal(
                storage.get() + offset,
                storage.get() + offset + length,
                other
            );
        }

        bool operator<(const json::cow_string& other) const noexcept {
            size_t n = std::min(size(), other.size());
            auto pa = data();
            auto pb = other.data();
            int cmp = std::char_traits<char>::compare(pa, pb, n);
            if (cmp < 0)
                return true;
            if (cmp > 0)
                return false;
            return size() < other.size();
        }

        // member functions
        cow_string substr(size_t offset, size_t count) const
        {
            cow_string sub(*this);
            sub.offset += offset;
            sub.length = count;
            return sub;
        }

        size_t find(char c, size_t pos = 0) const noexcept
        {
            for (size_t i = pos; i < length; i++)
            {
                if (storage[offset + i] == c)
                    return i;
            }
            return std::string::npos;
        }

        const char* data() const noexcept
        {
            return storage.get() + offset;
        }

        const char* c_str()
        {
            if (offset == 0 && storage[offset + length] == '\0')
                return storage.get();
            else
                copy();
            return storage.get() + offset;
        }

        size_t size() const noexcept 
        {
            return length; 
        }

    private:
        void copy()
        {
            auto new_data = std::make_shared<char[]>(length + 1);
            std::copy(
                storage.get() + offset,
                storage.get() + offset + length,
                new_data.get()
            );
            new_data[length] = '\0';
            storage = new_data;
            offset = 0;
        }

        size_t offset;
        size_t length;
        std::shared_ptr<char[]> storage;
    };

    // 支持输出到流
    std::ostream& operator<<(std::ostream& os, const cow_string& str)
    {
        os.write(str.data(), str.size());
        return os;
    }

    class json_exception : public std::runtime_error
    {
        public:
        explicit json_exception(const std::string& message) : 
            std::runtime_error(message) {}
    };

    struct node;

    using Null = std::monostate;
    using Bool = bool;
    using Integer = std::int64_t;
    using Float = double;
    using String = cow_string;
    using Array = std::vector<node>;
    using Object = std::map<String, node>;

    using Value = std::variant<Null, Bool, Integer, Float, String, Array, Object>;

    struct node
    {
        Value value;

        node() : value(Null{}) { }
        node(const Value& value) : value(value) { }
        node(Value&& value) : value(std::forward<Value>(value)) {}
        node& operator=(const Value& v) { value = v; return *this; }
        node& operator=(Value&& v) { value = std::forward<Value>(v); return *this; }

        auto& operator[](std::string_view key)
        {
            if (auto obj = std::get_if<Object>(&value))
                return (*obj)[key];

            throw std::runtime_error("not an object");
        }

        auto& operator[](const size_t index)
        {
            if (auto arr = std::get_if<Array>(&value))
                return (*arr)[index];

            throw std::runtime_error("not an array");
        }

        void push(const node& v) {
            if (auto arr = std::get_if<Array>(&value))
                arr->push_back(v);
        }
    };

    class json_reader
    {
    public:
        json_reader(std::string_view src) : 
            src(src), 
            pos(0),
            row(1),
            column(0)
        {}

        std::optional<node> parse()
        {
            pos = 0;
            row = 1;
            column = 0;
            return node{ parse_value() };
        }

    private:
        void skip_whitespace() noexcept
        {
            while (pos < src.size() && std::isspace(src[pos]))
            {
                if (src[pos] == '\n')
                {
                    row++;
                    column = 0;
                }
                pos++;
                column++;
            }
        }

        Value parse_null()
        {
            if (src.substr(pos, 4) == "null")
            {
                pos += 4;
                return Null{};
            }
            throw json_exception(std::format(
                "Illegal token '{}' found at position {}.",
                src[pos],
                pos)
            );
        }

        Value parse_true()
        {
            if (src.substr(pos, 4) == "true")
            {
                pos += 4;
                return true;
            }
            throw json_exception(std::format(
                "Illegal token '{}' found at position {}.",
                src[pos],
                pos)
            );
        }

        Value parse_false()
        {
            if (src.substr(pos, 5) == "false")
            {
                pos += 5;
                return false;
            }
            throw json_exception(std::format(
                "Illegal token '{}' found at position {}.",
                src[pos],
                pos)
            );
        }

        // 目前未实现转义序列
        Value parse_string()
        {
            pos++; // skip opening quote
            auto end = src.find('"', pos);
            if (end == src.npos)
                throw json_exception(std::format(
                    "Expected '\"' after string at position {}, but found '{}'.",
                    pos,
                    src[pos])
                );
            auto str = src.substr(pos, end - pos);
            pos = end + 1;
            return str;

        }

        Value parse_array()
        {
            pos++;
            auto arr = Array{};
            skip_whitespace();
            while (pos < src.size() && src[pos] != ']')
            {
                auto value = parse_value();

                if (pos < src.size() && src[pos] == ',')
                    pos++;
                skip_whitespace();

                arr.push_back(std::move(value));
            }
            pos++;
            return arr;
        }

        Value parse_object()
        {
            pos++;
            auto obj = Object{};
            skip_whitespace();
            while (pos < src.size() && src[pos] != '}')
            {
                auto key = parse_value();
                // if key is not a string
                if (!std::holds_alternative<String>(key))
                    throw json_exception(std::format(
                        "Key at position {} must be a string.",
                        pos)
                    );

                // middle must be a colon
                if (pos < src.size() && src[pos] == ':')
                    pos++;
                else
                    throw json_exception(std::format(
                        "Expected ':' after key at position {}, but found '{}'.",
                        pos,
                        src[pos])
                    );

                auto value = parse_value();

                if (pos < src.size() && src[pos] == ',')
                    pos++;
                skip_whitespace();

                obj[std::get<String>(key)] = node{ std::move(value) };  
            }
            pos++;
            return obj;
        }

        Value parse_number() {
            static auto is_float = [](const decltype(src)& number) {
                return
                    number.find('.') != number.npos ||
                    number.find('e') != number.npos;
            };

            auto end = pos;
            while (
                end < src.size() && 
                (
                    std::isdigit(src[end]) ||
                    src[end] == 'e' ||
                    src[end] == '.'
                )
            )
                end++;
            auto number = src.substr(pos, end - pos);
            pos = end;

            if (is_float(number))
            {
                Float val{};
                auto res = std::from_chars(number.data(), number.data() + number.size(), val);
                if (res.ec == std::errc{})
                    return val;
            }
            else
            {
                Integer val{};
                auto res = std::from_chars(number.data(), number.data() + number.size(), val);
                if (res.ec == std::errc{})
                    return val;
            }
            throw json_exception(std::format(
                "Invalid token {} at row {}, column {}.",
                src[pos], row, column
            ));
        }

        Value parse_value()
        {
            skip_whitespace();
            if (pos >= src.size())
                throw json_exception("Unexpected end of input.");
            switch (src[pos])
            {
                case 'n':
                    return parse_null();
                case 't':
                    return parse_true();
                case 'f':
                    return parse_false();
                case '"':
                    return parse_string();
                case '[':
                    return parse_array();
                case '{':
                    return parse_object();
                default:
                    return parse_number();
            }
            skip_whitespace();
        }

        cow_string src;
        size_t pos;
        size_t row, column;
    };

    class json_writer
    {
    public:
        json_writer(std::ostream& output) : 
            output(output)
        {}

        void write(node node)
        {
            std::visit([this](auto&& obj) {
                using T = std::decay_t<decltype(obj)>;
                if constexpr (std::is_same_v<T, Null>)
                {
                    output << "null";
                }
                else if constexpr (std::is_same_v<T, Bool>)
                {
                    output << (obj ? "true" : "false");
                }
                else if constexpr (std::is_same_v<T, Integer>)
                {
                    output << obj;
                }
                else if constexpr (std::is_same_v<T, Float>)
                {
                    output << obj;
                }
                else if constexpr (std::is_same_v<T, String>)
                {
                    output << '"' << obj.c_str() << '"';
                }
                else if constexpr (std::is_same_v<T, Array>)
                {
                    output << '[';
                    for (size_t i = 0; i < obj.size(); i++)
                    {
                        write(obj[i]);
                        if (i != obj.size() - 1)
                            output << ',';
                    }
                    output << ']';
                }
                else if constexpr (std::is_same_v<T, Object>)
                {
                    output << '{';
                    size_t i = 0;
                    for (const auto& [key, value] : obj)
                    {
                        output << '"' << key << "\":";
                        write(value);
                        if (i != obj.size() - 1)
                            output << ',';
                        i++;
                    }
                    output << '}';
                }
            }, node.value);
        }

    private:
        std::ostream& output;
    };
}

// 支持 std::format
namespace std
{
    template<>
    struct formatter<json::cow_string> : formatter<string_view>
    {
        auto format(const json::cow_string& str, auto& ctx) const
        {
            return formatter<string_view>().format(static_cast<string_view>(str), ctx);
        }
    };

    template<>
    struct formatter<json::cow_string::char_proxy> : formatter<string_view>
    {
        auto format(const json::cow_string::char_proxy& ch, auto& ctx) const
        {
            return formatter<char>().format(static_cast<char>(ch), ctx);
        }
    };
}

#endif
