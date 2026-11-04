#include "json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
    struct Parser
    {
        std::string_view s;
        size_t i = 0;

        void ws()
        {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            {
                i++;
            }
        }

        bool lit(std::string_view word)
        {
            if (s.substr(i, word.size()) == word)
            {
                i += word.size();
                return true;
            }
            return false;
        }

        static void utf8(std::string& out, unsigned cp)
        {
            if (cp < 0x80)
            {
                out += char(cp);
            }
            else if (cp < 0x800)
            {
                out += char(0xC0 | (cp >> 6));
                out += char(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                out += char(0xE0 | (cp >> 12));
                out += char(0x80 | ((cp >> 6) & 0x3F));
                out += char(0x80 | (cp & 0x3F));
            }
            else
            {
                out += char(0xF0 | (cp >> 18));
                out += char(0x80 | ((cp >> 12) & 0x3F));
                out += char(0x80 | ((cp >> 6) & 0x3F));
                out += char(0x80 | (cp & 0x3F));
            }
        }

        bool hex4(unsigned& v)
        {
            if (i + 4 > s.size())
            {
                return false;
            }
            v = 0;
            for (int k = 0; k < 4; k++)
            {
                char c = s[i++];
                v <<= 4;
                if (c >= '0' && c <= '9')
                {
                    v |= c - '0';
                }
                else if (c >= 'a' && c <= 'f')
                {
                    v |= c - 'a' + 10;
                }
                else if (c >= 'A' && c <= 'F')
                {
                    v |= c - 'A' + 10;
                }
                else
                {
                    return false;
                }
            }
            return true;
        }

        bool string(std::string& out)
        {
            if (i >= s.size() || s[i] != '"')
            {
                return false;
            }
            i++;
            while (i < s.size() && s[i] != '"')
            {
                char c = s[i++];
                if (c != '\\')
                {
                    out += c;
                    continue;
                }
                if (i >= s.size())
                {
                    return false;
                }
                char e = s[i++];
                switch (e)
                {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u':
                    {
                        unsigned cp;
                        if (!hex4(cp))
                        {
                            return false;
                        }
                        if (cp >= 0xD800 && cp < 0xDC00 && lit("\\u"))
                        {
                            unsigned lo;
                            if (!hex4(lo))
                            {
                                return false;
                            }
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8(out, cp);
                        break;
                    }
                    default: out += e; break;
                }
            }
            if (i >= s.size())
            {
                return false;
            }
            i++;
            return true;
        }

        bool value(Json& out, int depth)
        {
            if (depth > 64)
            {
                return false;
            }
            ws();
            if (i >= s.size())
            {
                return false;
            }
            char c = s[i];
            if (c == '{')
            {
                i++;
                out = Json::object();
                ws();
                if (i < s.size() && s[i] == '}')
                {
                    i++;
                    return true;
                }
                while (true)
                {
                    ws();
                    std::string key;
                    if (!string(key))
                    {
                        return false;
                    }
                    ws();
                    if (i >= s.size() || s[i] != ':')
                    {
                        return false;
                    }
                    i++;
                    Json v;
                    if (!value(v, depth + 1))
                    {
                        return false;
                    }
                    out.set(key, std::move(v));
                    ws();
                    if (i < s.size() && s[i] == ',')
                    {
                        i++;
                        continue;
                    }
                    if (i < s.size() && s[i] == '}')
                    {
                        i++;
                        return true;
                    }
                    return false;
                }
            }
            if (c == '[')
            {
                i++;
                out = Json::array();
                ws();
                if (i < s.size() && s[i] == ']')
                {
                    i++;
                    return true;
                }
                while (true)
                {
                    Json v;
                    if (!value(v, depth + 1))
                    {
                        return false;
                    }
                    out.push(std::move(v));
                    ws();
                    if (i < s.size() && s[i] == ',')
                    {
                        i++;
                        continue;
                    }
                    if (i < s.size() && s[i] == ']')
                    {
                        i++;
                        return true;
                    }
                    return false;
                }
            }
            if (c == '"')
            {
                std::string str;
                if (!string(str))
                {
                    return false;
                }
                out = Json(std::move(str));
                return true;
            }
            if (lit("true"))
            {
                out = Json(true);
                return true;
            }
            if (lit("false"))
            {
                out = Json(false);
                return true;
            }
            if (lit("null"))
            {
                out = Json();
                return true;
            }
            size_t start = i;
            while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.' ||
                                       s[i] == 'e' || s[i] == 'E'))
            {
                i++;
            }
            if (start == i)
            {
                return false;
            }
            out = Json(strtod(std::string(s.substr(start, i - start)).c_str(), nullptr));
            return true;
        }
    };

    void escape(std::string& out, const std::string& s)
    {
        out += '"';
        for (unsigned char c : s)
        {
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else
                    {
                        out += char(c);
                    }
            }
        }
        out += '"';
    }

    const Json nullJson;
}

bool Json::parse(std::string_view text, Json& out)
{
    Parser p{text};
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
    {
        p.i = 3;
    }
    if (!p.value(out, 0))
    {
        return false;
    }
    p.ws();
    return p.i == text.size();
}

const Json& Json::get(std::string_view key) const
{
    for (auto& m : members_)
    {
        if (m.first == key)
        {
            return m.second;
        }
    }
    return nullJson;
}

bool Json::has(std::string_view key) const
{
    for (auto& m : members_)
    {
        if (m.first == key)
        {
            return true;
        }
    }
    return false;
}

void Json::set(std::string_view key, Json v)
{
    type_ = Type::Object;
    for (auto& m : members_)
    {
        if (m.first == key)
        {
            m.second = std::move(v);
            return;
        }
    }
    members_.emplace_back(std::string(key), std::move(v));
}

void Json::erase(std::string_view key)
{
    for (auto it = members_.begin(); it != members_.end(); ++it)
    {
        if (it->first == key)
        {
            members_.erase(it);
            return;
        }
    }
}

std::string Json::dump() const
{
    std::string out;
    write(out);
    return out;
}

void Json::write(std::string& out) const
{
    switch (type_)
    {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number:
        {
            char buf[32];
            if (std::floor(num_) == num_ && std::fabs(num_) < 1e15)
            {
                snprintf(buf, sizeof(buf), "%lld", (long long)num_);
            }
            else
            {
                snprintf(buf, sizeof(buf), "%.17g", num_);
            }
            out += buf;
            break;
        }
        case Type::String: escape(out, str_); break;
        case Type::Array:
            out += '[';
            for (size_t k = 0; k < items_.size(); k++)
            {
                if (k)
                {
                    out += ',';
                }
                items_[k].write(out);
            }
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (size_t k = 0; k < members_.size(); k++)
            {
                if (k)
                {
                    out += ',';
                }
                escape(out, members_[k].first);
                out += ':';
                members_[k].second.write(out);
            }
            out += '}';
            break;
    }
}
