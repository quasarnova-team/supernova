/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * FxJson.h
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

#ifndef FX_INCLUDE_FXJSON_H_
#define FX_INCLUDE_FXJSON_H_

#include <map>
#include <memory>
#include <string>

namespace Fx
{

/* The FX connection services carry their configuration as a JSON object in a
 * String argument (see the Fx documentation for why: the spec's binary
 * ConnectionConfiguration structures are not portable across both OPC UA
 * backends). This codec implements the subset that projection needs — objects,
 * strings, numbers, booleans, null — and *refuses* everything else: arrays,
 * duplicate member names, surrogate escapes, unescaped control characters,
 * numbers JSON cannot represent, oversized or overly nested input. A
 * connection request that this parser accepts has exactly one meaning. */
class JsonValue
{
public:
    enum Kind
    {
        KindNull,
        KindBoolean,
        KindNumber,
        KindString,
        KindObject
    };

    JsonValue(): m_kind(KindNull), m_boolean(false), m_number(0) {}
    JsonValue(const JsonValue& other);
    JsonValue& operator=(const JsonValue& other);
    ~JsonValue();

    static JsonValue boolean(bool value);
    static JsonValue number(double value);
    static JsonValue string(const std::string& value);
    static JsonValue object();

    Kind kind() const { return m_kind; }
    bool isNull() const { return m_kind == KindNull; }

    /* Typed accessors throw std::runtime_error on a kind mismatch — the
     * services layer turns that into a refused call with a diagnostic. */
    bool asBoolean() const;
    double asNumber() const;
    const std::string& asString() const;

    bool hasMember(const std::string& name) const;
    const JsonValue& member(const std::string& name) const;
    void setMember(const std::string& name, const JsonValue& value);
    const std::map<std::string, JsonValue>& members() const;

    /* Deterministic output: members in lexicographic order, shortest
     * round-trip number form, minimal escaping. Throws on non-finite
     * numbers (JSON cannot carry them). */
    std::string serialize() const;

private:
    typedef std::map<std::string, JsonValue> Members;

    Members& requireObject();
    const Members& requireObject() const;

    Kind m_kind;
    bool m_boolean;
    double m_number;
    std::string m_string;
    std::unique_ptr<Members> m_members;
};

/* Parses exactly one JSON object — optional surrounding whitespace, nothing
 * else before or after. Limits: input at most 64 KiB, nesting at most 16
 * levels. Throws std::runtime_error naming the byte position and the cause. */
JsonValue parseJsonObject(const std::string& text);

}

#endif /* FX_INCLUDE_FXJSON_H_ */
