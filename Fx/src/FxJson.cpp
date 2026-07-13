/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * FxJson.cpp
 *
 *  Created on: 13 Jul 2026
 *      Author: Paris Moschovakos
 *
 *  This file is part of Quasar.
 *
 *  Quasar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public Licence as published by
 *  the Free Software Foundation, either version 3 of the Licence.
 *
 *  Quasar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public Licence for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Quasar.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <FxJson.h>

#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace Fx
{

namespace
{

/* A request larger than this is not a connection configuration. */
const size_t kMaxInputBytes = 64 * 1024;
/* Object nesting deeper than this is not a connection configuration. */
const int kMaxDepth = 16;

std::string positioned(size_t position, const std::string& what)
{
    std::ostringstream message;
    message << "JSON error at byte " << position << ": " << what;
    return message.str();
}

class Parser
{
public:
    Parser(const std::string& text): m_text(text), m_position(0), m_depth(0) {}

    JsonValue parseDocument()
    {
        skipWhitespace();
        if (m_position >= m_text.size() || m_text[m_position] != '{')
            fail("expected a JSON object");
        JsonValue value = parseObject();
        skipWhitespace();
        if (m_position != m_text.size())
            fail("trailing content after the JSON object");
        return value;
    }

private:
    void fail(const std::string& what) const
    {
        throw std::runtime_error(positioned(m_position, what));
    }

    void skipWhitespace()
    {
        while (m_position < m_text.size())
        {
            char c = m_text[m_position];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                break;
            m_position++;
        }
    }

    char peek() const
    {
        return m_position < m_text.size() ? m_text[m_position] : '\0';
    }

    void expect(char wanted, const char* what)
    {
        if (m_position >= m_text.size() || m_text[m_position] != wanted)
            fail(what);
        m_position++;
    }

    JsonValue parseObject()
    {
        if (++m_depth > kMaxDepth)
            fail("nesting deeper than the accepted maximum");
        expect('{', "expected '{'");
        JsonValue result = JsonValue::object();
        skipWhitespace();
        if (peek() == '}')
        {
            m_position++;
            m_depth--;
            return result;
        }
        for (;;)
        {
            skipWhitespace();
            if (peek() != '"')
                fail("expected a member name in double quotes");
            std::string name = parseString();
            if (result.hasMember(name))
                fail("duplicate member name '" + name + "'");
            skipWhitespace();
            expect(':', "expected ':' after the member name");
            skipWhitespace();
            result.setMember(name, parseValue());
            skipWhitespace();
            char next = peek();
            if (next == ',')
            {
                m_position++;
                continue;
            }
            if (next == '}')
            {
                m_position++;
                m_depth--;
                return result;
            }
            fail("expected ',' or '}' after a member");
        }
    }

    JsonValue parseValue()
    {
        char c = peek();
        if (c == '{')
            return parseObject();
        if (c == '"')
            return JsonValue::string(parseString());
        if (c == '[')
            fail("arrays are not part of the FX argument projection");
        if (c == 't' || c == 'f')
            return JsonValue::boolean(parseKeyword());
        if (c == 'n')
        {
            parseLiteral("null");
            return JsonValue();
        }
        if (c == '-' || (c >= '0' && c <= '9'))
            return JsonValue::number(parseNumber());
        fail("expected a value");
        return JsonValue(); /* unreachable */
    }

    bool parseKeyword()
    {
        if (peek() == 't')
        {
            parseLiteral("true");
            return true;
        }
        parseLiteral("false");
        return false;
    }

    void parseLiteral(const char* literal)
    {
        size_t start = m_position;
        for (const char* p = literal; *p; ++p)
        {
            if (m_position >= m_text.size() || m_text[m_position] != *p)
            {
                m_position = start;
                fail(std::string("expected '") + literal + "'");
            }
            m_position++;
        }
    }

    unsigned int parseHexQuad()
    {
        unsigned int value = 0;
        for (int i = 0; i < 4; i++)
        {
            if (m_position >= m_text.size())
                fail("truncated \\u escape");
            char c = m_text[m_position];
            unsigned int digit;
            if (c >= '0' && c <= '9')
                digit = static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f')
                digit = static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                digit = static_cast<unsigned int>(c - 'A' + 10);
            else
            {
                fail("invalid hex digit in \\u escape");
                return 0; /* unreachable */
            }
            value = value * 16 + digit;
            m_position++;
        }
        return value;
    }

    void appendUtf8(std::string& out, unsigned int codePoint)
    {
        if (codePoint < 0x80)
            out.push_back(static_cast<char>(codePoint));
        else if (codePoint < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    std::string parseString()
    {
        expect('"', "expected '\"'");
        std::string out;
        for (;;)
        {
            if (m_position >= m_text.size())
                fail("unterminated string");
            unsigned char c = static_cast<unsigned char>(m_text[m_position]);
            if (c == '"')
            {
                m_position++;
                return out;
            }
            if (c < 0x20)
                fail("unescaped control character in string");
            if (c != '\\')
            {
                out.push_back(static_cast<char>(c));
                m_position++;
                continue;
            }
            m_position++;
            if (m_position >= m_text.size())
                fail("truncated escape sequence");
            char escape = m_text[m_position];
            m_position++;
            switch (escape)
            {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u':
                {
                    unsigned int codePoint = parseHexQuad();
                    if (codePoint >= 0xD800 && codePoint <= 0xDFFF)
                        fail("surrogate \\u escapes are not supported (use raw UTF-8)");
                    appendUtf8(out, codePoint);
                    break;
                }
                default:
                    m_position -= 2;
                    fail("invalid escape sequence");
            }
        }
    }

    double parseNumber()
    {
        size_t start = m_position;
        if (peek() == '-')
            m_position++;
        /* integer part: '0' alone, or a nonzero digit followed by digits */
        if (peek() == '0')
            m_position++;
        else if (peek() >= '1' && peek() <= '9')
        {
            while (peek() >= '0' && peek() <= '9')
                m_position++;
        }
        else
        {
            m_position = start;
            fail("malformed number");
        }
        if (peek() == '.')
        {
            m_position++;
            if (!(peek() >= '0' && peek() <= '9'))
                fail("digits must follow the decimal point");
            while (peek() >= '0' && peek() <= '9')
                m_position++;
        }
        if (peek() == 'e' || peek() == 'E')
        {
            m_position++;
            if (peek() == '+' || peek() == '-')
                m_position++;
            if (!(peek() >= '0' && peek() <= '9'))
                fail("digits must follow the exponent");
            while (peek() >= '0' && peek() <= '9')
                m_position++;
        }

        /* The grammar above is exactly JSON's, so a classic-locale stream
         * conversion cannot mis-parse it (strtod would depend on the
         * process-wide LC_NUMERIC, which a server is free to change). */
        std::istringstream stream(m_text.substr(start, m_position - start));
        stream.imbue(std::locale::classic());
        double value = 0;
        stream >> value;
        if (stream.fail() || !stream.eof() || !(value == value)
            || value > 1.7976931348623157e308 || value < -1.7976931348623157e308)
        {
            m_position = start;
            fail("number out of range");
        }
        return value;
    }

    const std::string& m_text;
    size_t m_position;
    int m_depth;
};

void appendEscaped(std::string& out, const std::string& text)
{
    out.push_back('"');
    for (size_t i = 0; i < text.size(); i++)
    {
        unsigned char c = static_cast<unsigned char>(text[i]);
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                    out += buffer;
                }
                else
                    out.push_back(static_cast<char>(c));
        }
    }
    out.push_back('"');
}

std::string formatNumber(double value)
{
    if (!(value == value) || value > 1.7976931348623157e308 || value < -1.7976931348623157e308)
        throw std::runtime_error("JSON cannot represent a non-finite number");

    /* Integers up to 2^53 print without a fractional part. */
    if (value == std::floor(value) && value >= -9007199254740992.0 && value <= 9007199254740992.0)
    {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << static_cast<long long>(value);
        return stream.str();
    }

    /* Shortest form that round-trips: try 15 significant digits, verify by
     * re-parsing, fall back to 17 (always sufficient for IEEE 754 doubles). */
    for (int precision = 15; ; precision = 17)
    {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream.precision(precision);
        stream << value;
        std::istringstream back(stream.str());
        back.imbue(std::locale::classic());
        double reparsed = 0;
        back >> reparsed;
        if (reparsed == value || precision == 17)
            return stream.str();
    }
}

void serializeValue(const JsonValue& value, std::string& out)
{
    switch (value.kind())
    {
        case JsonValue::KindNull:
            out += "null";
            break;
        case JsonValue::KindBoolean:
            out += value.asBoolean() ? "true" : "false";
            break;
        case JsonValue::KindNumber:
            out += formatNumber(value.asNumber());
            break;
        case JsonValue::KindString:
            appendEscaped(out, value.asString());
            break;
        case JsonValue::KindObject:
        {
            out.push_back('{');
            const std::map<std::string, JsonValue>& members = value.members();
            for (std::map<std::string, JsonValue>::const_iterator it = members.begin();
                 it != members.end(); ++it)
            {
                if (it != members.begin())
                    out.push_back(',');
                appendEscaped(out, it->first);
                out.push_back(':');
                serializeValue(it->second, out);
            }
            out.push_back('}');
            break;
        }
    }
}

}

JsonValue::JsonValue(const JsonValue& other):
    m_kind(other.m_kind),
    m_boolean(other.m_boolean),
    m_number(other.m_number),
    m_string(other.m_string),
    m_members(other.m_members ? new Members(*other.m_members) : 0)
{
}

JsonValue& JsonValue::operator=(const JsonValue& other)
{
    if (this != &other)
    {
        m_kind = other.m_kind;
        m_boolean = other.m_boolean;
        m_number = other.m_number;
        m_string = other.m_string;
        m_members.reset(other.m_members ? new Members(*other.m_members) : 0);
    }
    return *this;
}

JsonValue::~JsonValue()
{
}

JsonValue JsonValue::boolean(bool value)
{
    JsonValue result;
    result.m_kind = KindBoolean;
    result.m_boolean = value;
    return result;
}

JsonValue JsonValue::number(double value)
{
    JsonValue result;
    result.m_kind = KindNumber;
    result.m_number = value;
    return result;
}

JsonValue JsonValue::string(const std::string& value)
{
    JsonValue result;
    result.m_kind = KindString;
    result.m_string = value;
    return result;
}

JsonValue JsonValue::object()
{
    JsonValue result;
    result.m_kind = KindObject;
    result.m_members.reset(new Members());
    return result;
}

bool JsonValue::asBoolean() const
{
    if (m_kind != KindBoolean)
        throw std::runtime_error("JSON value is not a boolean");
    return m_boolean;
}

double JsonValue::asNumber() const
{
    if (m_kind != KindNumber)
        throw std::runtime_error("JSON value is not a number");
    return m_number;
}

const std::string& JsonValue::asString() const
{
    if (m_kind != KindString)
        throw std::runtime_error("JSON value is not a string");
    return m_string;
}

JsonValue::Members& JsonValue::requireObject()
{
    if (m_kind != KindObject || !m_members)
        throw std::runtime_error("JSON value is not an object");
    return *m_members;
}

const JsonValue::Members& JsonValue::requireObject() const
{
    if (m_kind != KindObject || !m_members)
        throw std::runtime_error("JSON value is not an object");
    return *m_members;
}

bool JsonValue::hasMember(const std::string& name) const
{
    return requireObject().count(name) != 0;
}

const JsonValue& JsonValue::member(const std::string& name) const
{
    const Members& members = requireObject();
    Members::const_iterator it = members.find(name);
    if (it == members.end())
        throw std::runtime_error("JSON object has no member '" + name + "'");
    return it->second;
}

void JsonValue::setMember(const std::string& name, const JsonValue& value)
{
    requireObject()[name] = value;
}

const std::map<std::string, JsonValue>& JsonValue::members() const
{
    return requireObject();
}

std::string JsonValue::serialize() const
{
    std::string out;
    serializeValue(*this, out);
    return out;
}

JsonValue parseJsonObject(const std::string& text)
{
    if (text.size() > kMaxInputBytes)
        throw std::runtime_error("JSON error: input exceeds the accepted maximum size");
    Parser parser(text);
    return parser.parseDocument();
}

}
