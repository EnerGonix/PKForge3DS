// A small JSON value with a parser and a writer: enough for the Bank index and settings.
// Objects keep their key order, so files PKForge wrote come back out in the same shape.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class Json
{
public:
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Json() = default;
    Json(bool b) : type_(Type::Bool), bool_(b) {}
    Json(int n) : type_(Type::Number), num_(n) {}
    Json(double n) : type_(Type::Number), num_(n) {}
    Json(const char* s) : type_(Type::String), str_(s) {}
    Json(std::string s) : type_(Type::String), str_(std::move(s)) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }
    static bool parse(std::string_view text, Json& out);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }

    bool asBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    double asNumber(double fallback = 0) const { return type_ == Type::Number ? num_ : fallback; }
    int asInt(int fallback = 0) const { return type_ == Type::Number ? int(num_) : fallback; }
    const std::string& asString() const { return str_; }

    // Arrays
    size_t size() const { return type_ == Type::Array ? items_.size() : members_.size(); }
    const Json& operator[](size_t i) const { return items_[i]; }
    Json& operator[](size_t i) { return items_[i]; }
    void push(Json v) { type_ = Type::Array; items_.push_back(std::move(v)); }
    std::vector<Json>& items() { return items_; }
    const std::vector<Json>& items() const { return items_; }

    // Objects
    const Json& get(std::string_view key) const;
    bool has(std::string_view key) const;
    void set(std::string_view key, Json v);
    void erase(std::string_view key);
    const std::vector<std::pair<std::string, Json>>& members() const { return members_; }

    std::string dump() const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<Json> items_;
    std::vector<std::pair<std::string, Json>> members_;

    void write(std::string& out) const;
};
