/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * test_json.cpp
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
#include <stdexcept>
#include <string>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { \
        g_failures++; \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

using namespace Fx;

static bool parseFails(const std::string& text)
{
    try
    {
        parseJsonObject(text);
        return false;
    }
    catch (const std::runtime_error&)
    {
        return true;
    }
}

static void testHappyPath()
{
    JsonValue v = parseJsonObject(
        "{\"functionalEntity\":\"control\",\"role\":\"publisher\",\"dataset\":\"env\","
        "\"address\":\"opc.udp://239.192.0.20:4841\",\"publishingIntervalMs\":100.5,"
        "\"persistent\":true,\"comment\":null}");
    CHECK(v.kind() == JsonValue::KindObject);
    CHECK(v.at("functionalEntity").stringValue() == "control");
    CHECK(v.at("role").stringValue() == "publisher");
    CHECK(v.at("address").stringValue() == "opc.udp://239.192.0.20:4841");
    CHECK(v.at("publishingIntervalMs").numberValue() == 100.5);
    CHECK(v.at("persistent").boolValue() == true);
    CHECK(v.at("comment").kind() == JsonValue::KindNull);
    CHECK(v.has("role"));
    CHECK(!v.has("missing"));
}

static void testNestedPeer()
{
    JsonValue v = parseJsonObject(
        "{\"role\":\"subscriber\",\"peer\":{\"publisherId\":42,"
        "\"writerGroupId\":100,\"dataSetWriterId\":1,\"publisherIdType\":\"UInt16\"}}");
    const JsonValue& peer = v.at("peer");
    CHECK(peer.kind() == JsonValue::KindObject);
    CHECK(peer.at("publisherId").numberValue() == 42);
    CHECK(peer.at("writerGroupId").numberValue() == 100);
    CHECK(peer.at("dataSetWriterId").numberValue() == 1);
    CHECK(peer.at("publisherIdType").stringValue() == "UInt16");
}

static void testWhitespaceAndEscapes()
{
    JsonValue v = parseJsonObject(
        " {\n\t\"name\" : \"line\\nbreak \\\"quoted\\\" back\\\\slash \\u00e9\" }\r\n");
    CHECK(v.at("name").stringValue() == std::string("line\nbreak \"quoted\" back\\slash \xc3\xa9"));
}

static void testNumbers()
{
    JsonValue v = parseJsonObject(
        "{\"a\":0,\"b\":-17,\"c\":3.25,\"d\":1e3,\"e\":-2.5E-2}");
    CHECK(v.at("a").numberValue() == 0);
    CHECK(v.at("b").numberValue() == -17);
    CHECK(v.at("c").numberValue() == 3.25);
    CHECK(v.at("d").numberValue() == 1000);
    CHECK(v.at("e").numberValue() == -0.025);
}

static void testRejections()
{
    CHECK(parseFails(""));
    CHECK(parseFails("null"));
    CHECK(parseFails("42"));
    CHECK(parseFails("\"string\""));
    CHECK(parseFails("[1,2]"));
    CHECK(parseFails("{"));
    CHECK(parseFails("{}extra"));
    CHECK(parseFails("{\"a\":}"));
    CHECK(parseFails("{\"a\":1,}"));
    CHECK(parseFails("{\"a\" 1}"));
    CHECK(parseFails("{\"a\":[1]}"));
    CHECK(parseFails("{\"a\":tru}"));
    CHECK(parseFails("{\"a\":\"unterminated}"));
    CHECK(parseFails("{\"a\":\"bad \\x escape\"}"));
    CHECK(parseFails("{\"a\":\"\\ud800 surrogate\"}"));
    CHECK(parseFails("{\"a\":--1}"));
    CHECK(parseFails("{\"a\":1..2}"));
    CHECK(parseFails(std::string("{\"a\":\"raw\x01control\"}")));
}

static void testTypeMismatchThrows()
{
    JsonValue v = parseJsonObject("{\"n\":5,\"s\":\"x\",\"b\":false}");
    try { v.at("n").stringValue(); CHECK(false); } catch (const std::runtime_error&) { CHECK(true); }
    try { v.at("s").numberValue(); CHECK(false); } catch (const std::runtime_error&) { CHECK(true); }
    try { v.at("b").numberValue(); CHECK(false); } catch (const std::runtime_error&) { CHECK(true); }
    try { v.at("missing"); CHECK(false); } catch (const std::runtime_error&) { CHECK(true); }
}

static std::string nestedObjects(int depth)
{
    std::string text;
    for (int i = 0; i < depth - 1; i++)
        text += "{\"a\":";
    text += "{\"a\":1";
    for (int i = 0; i < depth - 1; i++)
        text += "}";
    text += "}";
    return text;
}

static void testHostileInputLimits()
{
    JsonValue ok = parseJsonObject(nestedObjects(16));
    CHECK(ok.kind() == JsonValue::KindObject);
    CHECK(parseFails(nestedObjects(17)));
    CHECK(parseFails(nestedObjects(5000)));
    std::string big = "{\"a\":\"" + std::string(70000, 'x') + "\"}";
    CHECK(parseFails(big));
    std::string underLimit = "{\"a\":\"" + std::string(60000, 'x') + "\"}";
    CHECK(parseJsonObject(underLimit).at("a").stringValue().size() == 60000);
}

static void testSerializeRoundTrip()
{
    JsonValue out = JsonValue::makeObject();
    out.set("status", JsonValue::makeString("Operational"));
    out.set("count", JsonValue::makeNumber(3));
    out.set("active", JsonValue::makeBool(true));
    out.set("note", JsonValue::makeString("tab\there \"q\""));
    JsonValue nested = JsonValue::makeObject();
    nested.set("publisherId", JsonValue::makeNumber(42));
    out.set("peer", nested);

    JsonValue back = parseJsonObject(out.serialize());
    CHECK(back.at("status").stringValue() == "Operational");
    CHECK(back.at("count").numberValue() == 3);
    CHECK(back.at("active").boolValue() == true);
    CHECK(back.at("note").stringValue() == "tab\there \"q\"");
    CHECK(back.at("peer").at("publisherId").numberValue() == 42);
}

int main()
{
    testHappyPath();
    testNestedPeer();
    testWhitespaceAndEscapes();
    testNumbers();
    testRejections();
    testTypeMismatchThrows();
    testHostileInputLimits();
    testSerializeRoundTrip();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
