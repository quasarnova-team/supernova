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

/* A deliberately small JSON subset for the FX connection-configuration
 * projection: objects, strings, numbers, booleans and null. No arrays —
 * the v0.x argument encoding does not need them, and the parser refuses
 * what it does not understand rather than guessing. */
class JsonValue
{
public:
    enum Kind { KindNull, KindBool, KindNumber, KindString, KindObject };

    JsonValue(): m_kind(KindNull), m_bool(false), m_number(0) {}

    static JsonValue makeBool(bool value);
    static JsonValue makeNumber(double value);
    static JsonValue makeString(const std::string& value);
    static JsonValue makeObject();

    Kind kind() const { return m_kind; }
    bool boolValue() const;
    double numberValue() const;
    const std::string& stringValue() const;

    bool has(const std::string& key) const;
    const JsonValue& at(const std::string& key) const;
    void set(const std::string& key, const JsonValue& value);
    const std::map<std::string, JsonValue>& members() const;

    std::string serialize() const;

private:
    Kind m_kind;
    bool m_bool;
    double m_number;
    std::string m_string;
    std::shared_ptr< std::map<std::string, JsonValue> > m_object;
};

/* Parses exactly one JSON object (with optional surrounding whitespace);
 * throws std::runtime_error naming position and cause on anything else. */
JsonValue parseJsonObject(const std::string& text);

std::string escapeJsonString(const std::string& text);

}

#endif /* FX_INCLUDE_FXJSON_H_ */
