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

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace Fx
{

JsonValue JsonValue::makeBool(bool value)
{
    JsonValue v;
    v.m_kind = KindBool;
    v.m_bool = value;
    return v;
}

JsonValue JsonValue::makeNumber(double value)
{
    JsonValue v;
    v.m_kind = KindNumber;
    v.m_number = value;
    return v;
}

JsonValue JsonValue::makeString(const std::string& value)
{
    JsonValue v;
    v.m_kind = KindString;
    v.m_string = value;
    return v;
}

JsonValue JsonValue::makeObject()
{
    JsonValue v;
    v.m_kind = KindObject;
    v.m_object.reset(new std::map<std::string, JsonValue>());
    return v;
}

bool JsonValue::boolValue() const
{
    if (m_kind != KindBool)
        throw std::runtime_error("json: value is not a boolean");
    return m_bool;
}

double JsonValue::numberValue() const
{
    if (m_kind != KindNumber)
        throw std::runtime_error("json: value is not a number");
    return m_number;
}

const std::string& JsonValue::stringValue() const
{
    if (m_kind != KindString)
        throw std::runtime_error("json: value is not a string");
    return m_string;
}

bool JsonValue::has(const std::string& key) const
{
    return m_kind == KindObject && m_object->count(key) > 0;
}

const JsonValue& JsonValue::at(const std::string& key) const
{
    if (m_kind != KindObject)
        throw std::runtime_error("json: value is not an object");
    std::map<std::string, JsonValue>::const_iterator it = m_object->find(key);
    if (it == m_object->end())
        throw std::runtime_error("json: missing member '" + key + "'");
    return it->second;
}

void JsonValue::set(const std::string& key, const JsonValue& value)
{
    if (m_kind != KindObject)
        throw std::runtime_error("json: value is not an object");
    (*m_object)[key] = value;
}

const std::map<std::string, JsonValue>& JsonValue::members() const
{
    if (m_kind != KindObject)
        throw std::runtime_error("json: value is not an object");
    return *m_object;
}

std::string escapeJsonString(const std::string& text)
{
    std::ostringstream out;
    for (size_t i = 0; i < text.size(); i++)
    {
        unsigned char c = static_cast<unsigned char>(text[i]);
        switch (c)
        {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                    out << buffer;
                }
                else
                    out << text[i];
        }
    }
    return out.str();
}

std::string JsonValue::serialize() const
{
    switch (m_kind)
    {
        case KindNull:   return "null";
        case KindBool:   return m_bool ? "true" : "false";
        case KindNumber:
        {
            std::ostringstream out;
            if (m_number == static_cast<double>(static_cast<long long>(m_number)))
                out << static_cast<long long>(m_number);
            else
                out << m_number;
            return out.str();
        }
        case KindString: return "\"" + escapeJsonString(m_string) + "\"";
        case KindObject:
        {
            std::ostringstream out;
            out << "{";
            bool first = true;
            for (std::map<std::string, JsonValue>::const_iterator it = m_object->begin();
                 it != m_object->end(); ++it)
            {
                if (!first)
                    out << ",";
                first = false;
                out << "\"" << escapeJsonString(it->first) << "\":" << it->second.serialize();
            }
            out << "}";
            return out.str();
        }
    }
    throw std::runtime_error("json: unreachable kind");
}

namespace
{

class Parser
{
public:
    Parser(const std::string& text): m_text(text), m_pos(0) {}

    JsonValue parseDocument()
    {
        skipWhitespace();
        JsonValue value = parseObject();
        skipWhitespace();
        if (m_pos != m_text.size())
            fail("trailing content after the object");
        return value;
    }

private:
    void fail(const std::string& why) const
    {
        std::ostringstream out;
        out << "json: " << why << " (at offset " << m_pos << ")";
        throw std::runtime_error(out.str());
    }

    void skipWhitespace()
    {
        while (m_pos < m_text.size())
        {
            char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                m_pos++;
            else
                break;
        }
    }

    char peek() const
    {
        if (m_pos >= m_text.size())
            throw std::runtime_error("json: unexpected end of input");
        return m_text[m_pos];
    }

    void expect(char c)
    {
        if (peek() != c)
            fail(std::string("expected '") + c + "'");
        m_pos++;
    }

    bool consumeIf(char c)
    {
        if (m_pos < m_text.size() && m_text[m_pos] == c)
        {
            m_pos++;
            return true;
        }
        return false;
    }

    JsonValue parseObject()
    {
        expect('{');
        JsonValue object = JsonValue::makeObject();
        skipWhitespace();
        if (consumeIf('}'))
            return object;
        while (true)
        {
            skipWhitespace();
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            skipWhitespace();
            object.set(key, parseValue());
            skipWhitespace();
            if (consumeIf(','))
                continue;
            expect('}');
            return object;
        }
    }

    JsonValue parseValue()
    {
        char c = peek();
        if (c == '{')
            return parseObject();
        if (c == '"')
            return JsonValue::makeString(parseString());
        if (c == 't' || c == 'f')
            return parseBool();
        if (c == 'n')
        {
            parseLiteral("null");
            return JsonValue();
        }
        if (c == '-' || (c >= '0' && c <= '9'))
            return parseNumber();
        if (c == '[')
            fail("arrays are not part of the FX argument encoding");
        fail("unexpected character");
        return JsonValue();
    }

    void parseLiteral(const char* literal)
    {
        for (const char* p = literal; *p; ++p)
        {
            if (m_pos >= m_text.size() || m_text[m_pos] != *p)
                fail(std::string("broken literal, expected '") + literal + "'");
            m_pos++;
        }
    }

    JsonValue parseBool()
    {
        if (peek() == 't')
        {
            parseLiteral("true");
            return JsonValue::makeBool(true);
        }
        parseLiteral("false");
        return JsonValue::makeBool(false);
    }

    JsonValue parseNumber()
    {
        size_t start = m_pos;
        if (consumeIf('-')) {}
        while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9')
            m_pos++;
        if (consumeIf('.'))
        {
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9')
                m_pos++;
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E'))
        {
            m_pos++;
            if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-'))
                m_pos++;
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9')
                m_pos++;
        }
        std::string token = m_text.substr(start, m_pos - start);
        if (token.empty() || token == "-")
            fail("broken number");
        char* end = 0;
        double value = std::strtod(token.c_str(), &end);
        if (!end || *end != '\0')
            fail("broken number '" + token + "'");
        return JsonValue::makeNumber(value);
    }

    std::string parseString()
    {
        expect('"');
        std::string out;
        while (true)
        {
            if (m_pos >= m_text.size())
                fail("unterminated string");
            char c = m_text[m_pos++];
            if (c == '"')
                return out;
            if (static_cast<unsigned char>(c) < 0x20)
                fail("raw control character inside a string");
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (m_pos >= m_text.size())
                fail("unterminated escape");
            char e = m_text[m_pos++];
            switch (e)
            {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u':
                {
                    if (m_pos + 4 > m_text.size())
                        fail("broken \\u escape");
                    unsigned int code = 0;
                    for (int i = 0; i < 4; i++)
                    {
                        char h = m_text[m_pos++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= static_cast<unsigned int>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned int>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned int>(h - 'A' + 10);
                        else fail("broken \\u escape");
                    }
                    if (code >= 0xD800 && code <= 0xDFFF)
                        fail("surrogate pairs are not supported in the FX argument encoding");
                    if (code < 0x80)
                        out += static_cast<char>(code);
                    else if (code < 0x800)
                    {
                        out += static_cast<char>(0xC0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    else
                    {
                        out += static_cast<char>(0xE0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default:
                    fail("unknown escape");
            }
        }
    }

    const std::string& m_text;
    size_t m_pos;
};

}

JsonValue parseJsonObject(const std::string& text)
{
    Parser parser(text);
    return parser.parseDocument();
}

}
