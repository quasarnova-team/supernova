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

static bool parses(const std::string& text)
{
    try
    {
        parseJsonObject(text);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

static std::string diagnosticOf(const std::string& text)
{
    try
    {
        parseJsonObject(text);
        return "";
    }
    catch (const std::exception& e)
    {
        return e.what();
    }
}

static void testAcceptedDocuments()
{
    CHECK(parses("{}"));
    CHECK(parses("  {  }  "));
    CHECK(parses("{\"a\":1}"));
    CHECK(parses("\t\r\n {\"a\": {\"b\": {\"c\": \"deep\"}}} \n"));
    CHECK(parses("{\"s\":\"text\",\"n\":-2.5e3,\"b\":true,\"z\":false,\"v\":null}"));

    JsonValue value = parseJsonObject("{\"role\":\"publisher\",\"interval\":12.5,\"peer\":{\"publisherId\":91}}");
    CHECK(value.kind() == JsonValue::KindObject);
    CHECK(value.hasMember("role"));
    CHECK(value.member("role").asString() == "publisher");
    CHECK(value.member("interval").asNumber() == 12.5);
    CHECK(value.member("peer").kind() == JsonValue::KindObject);
    CHECK(value.member("peer").member("publisherId").asNumber() == 91.0);
    CHECK(!value.hasMember("absent"));
}

static void testRefusedDocuments()
{
    /* not an object at the top */
    CHECK(!parses(""));
    CHECK(!parses("42"));
    CHECK(!parses("\"text\""));
    CHECK(!parses("true"));
    CHECK(!parses("null"));
    CHECK(!parses("[]"));
    CHECK(!parses("[{}]"));

    /* malformed structure */
    CHECK(!parses("{"));
    CHECK(!parses("}"));
    CHECK(!parses("{\"a\"}"));
    CHECK(!parses("{\"a\":}"));
    CHECK(!parses("{\"a\":1,}"));
    CHECK(!parses("{,\"a\":1}"));
    CHECK(!parses("{\"a\":1 \"b\":2}"));
    CHECK(!parses("{'a':1}"));
    CHECK(!parses("{a:1}"));
    CHECK(!parses("{} {}"));
    CHECK(!parses("{}x"));

    /* arrays are refused wherever they appear */
    CHECK(!parses("{\"a\":[]}"));
    CHECK(!parses("{\"a\":[1,2]}"));
    CHECK(diagnosticOf("{\"a\":[]}").find("array") != std::string::npos);

    /* duplicate members are ambiguous — refused */
    CHECK(!parses("{\"a\":1,\"a\":2}"));
    CHECK(diagnosticOf("{\"a\":1,\"a\":2}").find("duplicate") != std::string::npos);

    /* broken literals */
    CHECK(!parses("{\"a\":tru}"));
    CHECK(!parses("{\"a\":falsy}"));
    CHECK(!parses("{\"a\":nul}"));
    CHECK(!parses("{\"a\":True}"));
}

static void testStrings()
{
    CHECK(parseJsonObject("{\"s\":\"\"}").member("s").asString() == "");
    CHECK(parseJsonObject("{\"s\":\"plain\"}").member("s").asString() == "plain");
    CHECK(parseJsonObject("{\"s\":\"a\\\"b\"}").member("s").asString() == "a\"b");
    CHECK(parseJsonObject("{\"s\":\"a\\\\b\"}").member("s").asString() == "a\\b");
    CHECK(parseJsonObject("{\"s\":\"a\\/b\"}").member("s").asString() == "a/b");
    CHECK(parseJsonObject("{\"s\":\"\\b\\f\\n\\r\\t\"}").member("s").asString() == "\b\f\n\r\t");
    CHECK(parseJsonObject("{\"s\":\"\\u0041\"}").member("s").asString() == "A");
    /* 2- and 3-byte UTF-8 from \u escapes */
    CHECK(parseJsonObject("{\"s\":\"\\u00e9\"}").member("s").asString() == "\xC3\xA9");
    CHECK(parseJsonObject("{\"s\":\"\\u20ac\"}").member("s").asString() == "\xE2\x82\xAC");
    /* raw UTF-8 passes through untouched */
    CHECK(parseJsonObject("{\"s\":\"caf\xC3\xA9\"}").member("s").asString() == "caf\xC3\xA9");

    CHECK(!parses("{\"s\":\"unterminated"));
    CHECK(!parses("{\"s\":\"bad\\q\"}"));
    CHECK(!parses("{\"s\":\"trunc\\u12\"}"));
    CHECK(!parses("{\"s\":\"nothex\\uZZZZ\"}"));
    CHECK(!parses("{\"s\":\"\\ud834\\udd1e\"}"));  /* surrogate pair — refused */
    CHECK(!parses("{\"s\":\"\\udc00\"}"));          /* lone surrogate — refused */
    CHECK(!parses(std::string("{\"s\":\"a\nb\"}"))); /* raw control char */
    CHECK(!parses(std::string("{\"s\":\"a\x01") + "b\"}"));
}

static void testNumbers()
{
    CHECK(parseJsonObject("{\"n\":0}").member("n").asNumber() == 0.0);
    CHECK(parseJsonObject("{\"n\":-0}").member("n").asNumber() == 0.0);
    CHECK(parseJsonObject("{\"n\":42}").member("n").asNumber() == 42.0);
    CHECK(parseJsonObject("{\"n\":-17}").member("n").asNumber() == -17.0);
    CHECK(parseJsonObject("{\"n\":3.25}").member("n").asNumber() == 3.25);
    CHECK(parseJsonObject("{\"n\":1e3}").member("n").asNumber() == 1000.0);
    CHECK(parseJsonObject("{\"n\":1E-2}").member("n").asNumber() == 0.01);
    CHECK(parseJsonObject("{\"n\":2.5e+2}").member("n").asNumber() == 250.0);
    CHECK(parseJsonObject("{\"n\":9007199254740992}").member("n").asNumber() == 9007199254740992.0);

    CHECK(!parses("{\"n\":01}"));      /* leading zero */
    CHECK(!parses("{\"n\":+1}"));      /* leading plus */
    CHECK(!parses("{\"n\":.5}"));      /* bare fraction */
    CHECK(!parses("{\"n\":1.}"));      /* trailing point */
    CHECK(!parses("{\"n\":1e}"));      /* empty exponent */
    CHECK(!parses("{\"n\":0x10}"));    /* hex */
    CHECK(!parses("{\"n\":NaN}"));
    CHECK(!parses("{\"n\":Infinity}"));
    CHECK(!parses("{\"n\":-}"));
    CHECK(!parses("{\"n\":1e999}"));   /* overflows double */
}

static void testLimits()
{
    /* depth 16 (15 nested inside the root) is accepted, 17 is refused */
    std::string deepOk;
    for (int i = 0; i < 15; i++)
        deepOk += "{\"a\":";
    deepOk += "{}";
    for (int i = 0; i < 15; i++)
        deepOk += "}";
    CHECK(parses(deepOk));

    std::string deepBad;
    for (int i = 0; i < 16; i++)
        deepBad += "{\"a\":";
    deepBad += "{}";
    for (int i = 0; i < 16; i++)
        deepBad += "}";
    CHECK(!parses(deepBad));
    CHECK(diagnosticOf(deepBad).find("nesting") != std::string::npos);

    /* a nesting bomb far past the limit fails fast, not by stack overflow */
    CHECK(!parses(std::string(5000, '{')));

    /* the size ceiling */
    std::string big = "{\"a\":\"" + std::string(70 * 1024, 'x') + "\"}";
    CHECK(!parses(big));
    CHECK(diagnosticOf(big).find("size") != std::string::npos);

    /* diagnostics carry the byte position */
    CHECK(diagnosticOf("{\"a\":x}").find("byte 5") != std::string::npos);
}

static void testAccessorDiscipline()
{
    JsonValue value = parseJsonObject("{\"s\":\"x\",\"n\":1,\"b\":true,\"v\":null}");

    bool threw = false;
    try { value.member("s").asNumber(); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { value.member("n").asString(); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { value.member("b").asString(); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { value.member("absent"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { value.member("s").member("x"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    CHECK(value.member("v").isNull());
    CHECK(!value.member("s").isNull());
}

static void testBuildAndSerialize()
{
    JsonValue out = JsonValue::object();
    out.setMember("status", JsonValue::string("Operational"));
    out.setMember("count", JsonValue::number(3));
    out.setMember("active", JsonValue::boolean(true));
    JsonValue peer = JsonValue::object();
    peer.setMember("publisherId", JsonValue::number(91));
    out.setMember("peer", peer);

    /* deterministic: members in lexicographic order */
    CHECK(out.serialize() ==
        "{\"active\":true,\"count\":3,\"peer\":{\"publisherId\":91},\"status\":\"Operational\"}");

    /* overwrite keeps one member */
    out.setMember("count", JsonValue::number(4));
    CHECK(out.member("count").asNumber() == 4.0);

    /* deep copies, not shared structure */
    JsonValue copy = out;
    copy.setMember("count", JsonValue::number(5));
    CHECK(out.member("count").asNumber() == 4.0);
    CHECK(copy.member("count").asNumber() == 5.0);

    /* escaping on output */
    JsonValue tricky = JsonValue::object();
    tricky.setMember("s", JsonValue::string("a\"b\\c\nd\x01"));
    CHECK(tricky.serialize() == "{\"s\":\"a\\\"b\\\\c\\nd\\u0001\"}");

    /* numbers: integers stay integers, doubles round-trip */
    JsonValue numbers = JsonValue::object();
    numbers.setMember("i", JsonValue::number(42));
    numbers.setMember("d", JsonValue::number(0.1));
    JsonValue reparsed = parseJsonObject(numbers.serialize());
    CHECK(reparsed.member("i").asNumber() == 42.0);
    CHECK(reparsed.member("d").asNumber() == 0.1);
    CHECK(numbers.serialize().find("\"i\":42") != std::string::npos);

    /* non-finite numbers cannot be serialized */
    JsonValue bad = JsonValue::object();
    bad.setMember("n", JsonValue::number(1e308 * 10));
    bool threw = false;
    try { bad.serialize(); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    /* full round-trip stability */
    const std::string canonical =
        "{\"address\":\"opc.udp://239.0.0.5:4840\",\"dataset\":\"env\",\"role\":\"publisher\"}";
    CHECK(parseJsonObject(canonical).serialize() == canonical);
}

int main()
{
    testAcceptedDocuments();
    testRefusedDocuments();
    testStrings();
    testNumbers();
    testLimits();
    testAccessorDiscipline();
    testBuildAndSerialize();

    std::printf("%s: %d checks, %d failure(s)\n",
                g_failures == 0 ? "PASS" : "FAIL", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
