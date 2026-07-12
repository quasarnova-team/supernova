/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * test_wire.cpp
 *
 *  Created on: 12 Jul 2026
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

#include <PubSubWire.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { \
        g_failures++; \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

using namespace PubSub;

static NetworkMessage roundTrip(const NetworkMessage& in, bool& ok, std::string& diagnostic)
{
    std::vector<uint8_t> wire = encodeNetworkMessage(in);
    NetworkMessage out;
    ok = decodeNetworkMessage(wire.empty() ? 0 : &wire[0], wire.size(), out, diagnostic);
    return out;
}

static void testGoldenBytes()
{
    NetworkMessage m;
    m.publisherIdType = PublisherIdUInt16;
    m.publisherId = 2234;
    m.writerGroupId = 100;
    m.groupSequenceNumber = 1;
    DataSetMessage dsm;
    dsm.dataSetWriterId = 62541;
    dsm.sequenceNumberEnabled = true;
    dsm.sequenceNumber = 1;
    dsm.fields.push_back(WireValue::makeSigned(TypeInt32, 7));
    m.messages.push_back(dsm);

    const uint8_t expected[] = {
        0xF1, 0x01, 0xBA, 0x08, 0x09, 0x64, 0x00, 0x01, 0x00,
        0x01, 0x4D, 0xF4,
        0x09, 0x01, 0x00, 0x01, 0x00, 0x06, 0x07, 0x00, 0x00, 0x00
    };

    std::vector<uint8_t> wire = encodeNetworkMessage(m);
    CHECK(wire.size() == sizeof expected);
    if (wire.size() == sizeof expected)
    {
        for (size_t i = 0; i < wire.size(); i++)
        {
            if (wire[i] != expected[i])
            {
                g_failures++;
                std::printf("FAIL golden byte %zu: got %02X want %02X\n", i, wire[i], expected[i]);
            }
        }
        g_checks++;
    }
}

static void testScalarRoundTrips()
{
    NetworkMessage m;
    m.publisherIdType = PublisherIdUInt16;
    m.publisherId = 42;
    m.writerGroupId = 7;
    m.groupSequenceNumber = 3;
    DataSetMessage dsm;
    dsm.dataSetWriterId = 1;
    dsm.sequenceNumberEnabled = true;
    dsm.sequenceNumber = 99;
    dsm.fields.push_back(WireValue::makeNull());
    dsm.fields.push_back(WireValue::makeBoolean(true));
    dsm.fields.push_back(WireValue::makeSigned(TypeSByte, -5));
    dsm.fields.push_back(WireValue::makeUnsigned(TypeByte, 200));
    dsm.fields.push_back(WireValue::makeSigned(TypeInt16, -30000));
    dsm.fields.push_back(WireValue::makeUnsigned(TypeUInt16, 60000));
    dsm.fields.push_back(WireValue::makeSigned(TypeInt32, -2000000000));
    dsm.fields.push_back(WireValue::makeUnsigned(TypeUInt32, 4000000000u));
    dsm.fields.push_back(WireValue::makeSigned(TypeInt64, -9000000000000000000LL));
    dsm.fields.push_back(WireValue::makeUnsigned(TypeUInt64, 18000000000000000000ULL));
    dsm.fields.push_back(WireValue::makeFloat(3.5f));
    dsm.fields.push_back(WireValue::makeDouble(-2.25e-10));
    dsm.fields.push_back(WireValue::makeString("supernova"));
    dsm.fields.push_back(WireValue::makeString(""));
    dsm.fields.push_back(WireValue::makeDateTime(133774531200000000LL));
    m.messages.push_back(dsm);

    bool ok = false;
    std::string diagnostic;
    NetworkMessage out = roundTrip(m, ok, diagnostic);
    CHECK(ok);
    if (!ok) { std::printf("  diagnostic: %s\n", diagnostic.c_str()); return; }
    CHECK(out.publisherIdType == PublisherIdUInt16);
    CHECK(out.publisherId == 42);
    CHECK(out.writerGroupIdEnabled && out.writerGroupId == 7);
    CHECK(out.groupSequenceNumberEnabled && out.groupSequenceNumber == 3);
    CHECK(out.messages.size() == 1);
    if (out.messages.size() != 1) return;
    const DataSetMessage& d = out.messages[0];
    CHECK(d.dataSetWriterId == 1);
    CHECK(d.sequenceNumberEnabled && d.sequenceNumber == 99);
    CHECK(d.fields.size() == m.messages[0].fields.size());
    for (size_t i = 0; i < d.fields.size() && i < m.messages[0].fields.size(); i++)
        CHECK(d.fields[i].equals(m.messages[0].fields[i]));
}

static void testPublisherIdTypes()
{
    struct { PublisherIdType type; uint64_t id; uint8_t expectedFlags; } cases[] = {
        { PublisherIdByte,   200,                   0x71 },
        { PublisherIdUInt16, 60000,                 0xF1 },
        { PublisherIdUInt32, 4000000000u,           0xF1 },
        { PublisherIdUInt64, 18000000000000000000ULL, 0xF1 },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
        NetworkMessage m;
        m.publisherIdType = cases[i].type;
        m.publisherId = cases[i].id;
        m.writerGroupId = 1;
        DataSetMessage dsm;
        dsm.dataSetWriterId = 5;
        dsm.fields.push_back(WireValue::makeBoolean(false));
        m.messages.push_back(dsm);

        std::vector<uint8_t> wire = encodeNetworkMessage(m);
        CHECK(!wire.empty() && wire[0] == cases[i].expectedFlags);

        bool ok = false;
        std::string diagnostic;
        NetworkMessage out;
        ok = decodeNetworkMessage(&wire[0], wire.size(), out, diagnostic);
        CHECK(ok);
        CHECK(out.publisherIdType == cases[i].type);
        CHECK(out.publisherId == cases[i].id);
    }
}

static void testMultipleDataSetMessages()
{
    NetworkMessage m;
    m.publisherIdType = PublisherIdByte;
    m.publisherId = 9;
    m.writerGroupId = 300;
    m.groupSequenceNumber = 77;
    for (int w = 0; w < 3; w++)
    {
        DataSetMessage dsm;
        dsm.dataSetWriterId = static_cast<uint16_t>(1000 + w);
        dsm.sequenceNumberEnabled = true;
        dsm.sequenceNumber = static_cast<uint16_t>(w);
        dsm.fields.push_back(WireValue::makeSigned(TypeInt32, w * 11));
        dsm.fields.push_back(WireValue::makeString(std::string(static_cast<size_t>(w + 1), 'x')));
        m.messages.push_back(dsm);
    }

    bool ok = false;
    std::string diagnostic;
    NetworkMessage out = roundTrip(m, ok, diagnostic);
    CHECK(ok);
    if (!ok) { std::printf("  diagnostic: %s\n", diagnostic.c_str()); return; }
    CHECK(out.messages.size() == 3);
    for (size_t w = 0; w < out.messages.size(); w++)
    {
        CHECK(out.messages[w].dataSetWriterId == 1000 + w);
        CHECK(out.messages[w].sequenceNumber == w);
        CHECK(out.messages[w].fields.size() == 2);
        if (out.messages[w].fields.size() == 2)
        {
            CHECK(out.messages[w].fields[0].signedValue() == static_cast<int64_t>(w) * 11);
            CHECK(out.messages[w].fields[1].stringValue() == std::string(w + 1, 'x'));
        }
    }
}

static void testTruncationSafety()
{
    NetworkMessage m;
    m.publisherIdType = PublisherIdUInt64;
    m.publisherId = 12345678901234ULL;
    m.writerGroupId = 100;
    m.groupSequenceNumber = 1;
    for (int w = 0; w < 2; w++)
    {
        DataSetMessage dsm;
        dsm.dataSetWriterId = static_cast<uint16_t>(w + 1);
        dsm.sequenceNumberEnabled = true;
        dsm.sequenceNumber = 4;
        dsm.fields.push_back(WireValue::makeDouble(1.5));
        dsm.fields.push_back(WireValue::makeString("truncation"));
        m.messages.push_back(dsm);
    }
    std::vector<uint8_t> wire = encodeNetworkMessage(m);

    bool ok = false;
    std::string diagnostic;
    NetworkMessage full;
    ok = decodeNetworkMessage(&wire[0], wire.size(), full, diagnostic);
    CHECK(ok);

    for (size_t cut = 0; cut < wire.size(); cut++)
    {
        NetworkMessage out;
        std::string diag;
        bool decoded = decodeNetworkMessage(cut == 0 ? 0 : &wire[0], cut, out, diag);
        if (decoded)
        {
            g_failures++;
            std::printf("FAIL truncated decode at %zu bytes unexpectedly succeeded\n", cut);
        }
        g_checks++;
    }
}

static void testForeignHeaderFeatures()
{
    std::vector<uint8_t> wire;
    wire.push_back(0xF1);
    wire.push_back(0x01 | 0x20 | 0x80);
    wire.push_back(0x00);
    wire.push_back(0xBA); wire.push_back(0x08);
    wire.push_back(0x0F);
    wire.push_back(0xE8); wire.push_back(0x03);
    wire.push_back(0x2A); wire.push_back(0x00); wire.push_back(0x00); wire.push_back(0x00);
    wire.push_back(0x05); wire.push_back(0x00);
    wire.push_back(0x02); wire.push_back(0x00);
    wire.push_back(0x01);
    wire.push_back(0x4D); wire.push_back(0xF4);
    for (int i = 0; i < 8; i++) wire.push_back(0x11);
    uint8_t flags1 = 0x01 | (0 << 1) | 0x08 | 0x10 | 0x20 | 0x40 | 0x80;
    wire.push_back(flags1);
    wire.push_back(0x00 | 0x10);
    wire.push_back(0x09); wire.push_back(0x00);
    for (int i = 0; i < 8; i++) wire.push_back(0x22);
    wire.push_back(0x00); wire.push_back(0x80);
    wire.push_back(0x01); wire.push_back(0x00); wire.push_back(0x00); wire.push_back(0x00);
    wire.push_back(0x02); wire.push_back(0x00); wire.push_back(0x00); wire.push_back(0x00);
    wire.push_back(0x01); wire.push_back(0x00);
    wire.push_back(0x0B);
    double d = 21.75;
    uint64_t bits = 0;
    unsigned char raw[8];
    std::memcpy(raw, &d, 8);
    for (int i = 0; i < 8; i++) bits |= static_cast<uint64_t>(raw[i]) << (8 * i);
    for (int i = 0; i < 8; i++) wire.push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xFF));

    NetworkMessage out;
    std::string diagnostic;
    bool ok = decodeNetworkMessage(&wire[0], wire.size(), out, diagnostic);
    CHECK(ok);
    if (!ok) { std::printf("  diagnostic: %s\n", diagnostic.c_str()); return; }
    CHECK(out.publisherId == 2234);
    CHECK(out.writerGroupIdEnabled && out.writerGroupId == 1000);
    CHECK(out.groupSequenceNumberEnabled && out.groupSequenceNumber == 2);
    CHECK(out.messages.size() == 1);
    if (out.messages.size() == 1)
    {
        CHECK(out.messages[0].dataSetWriterId == 62541);
        CHECK(out.messages[0].sequenceNumber == 9);
        CHECK(out.messages[0].fields.size() == 1);
        if (out.messages[0].fields.size() == 1)
        {
            CHECK(out.messages[0].fields[0].type() == TypeDouble);
            CHECK(out.messages[0].fields[0].floatValue() == 21.75);
        }
    }
}

static void testKeepAliveDecode()
{
    std::vector<uint8_t> wire;
    wire.push_back(0x71);
    wire.push_back(0x07);
    wire.push_back(0x01);
    wire.push_back(0x64); wire.push_back(0x00);
    wire.push_back(0x01);
    wire.push_back(0x01); wire.push_back(0x00);
    wire.push_back(0x01 | 0x80);
    wire.push_back(0x03);

    NetworkMessage out;
    std::string diagnostic;
    bool ok = decodeNetworkMessage(&wire[0], wire.size(), out, diagnostic);
    CHECK(ok);
    if (!ok) { std::printf("  diagnostic: %s\n", diagnostic.c_str()); return; }
    CHECK(out.messages.size() == 1);
    if (out.messages.size() == 1)
    {
        CHECK(out.messages[0].keepAlive);
        CHECK(out.messages[0].fields.empty());
    }
}

static void testRejections()
{
    NetworkMessage empty;
    bool threw = false;
    try { encodeNetworkMessage(empty); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw);

    std::vector<uint8_t> secured;
    secured.push_back(0xF1);
    secured.push_back(0x01 | 0x10);
    secured.push_back(0x2A); secured.push_back(0x00);
    secured.push_back(0x01);
    secured.push_back(0x64); secured.push_back(0x00);
    secured.push_back(0x01);
    secured.push_back(0x01); secured.push_back(0x00);
    NetworkMessage out;
    std::string diagnostic;
    CHECK(!decodeNetworkMessage(&secured[0], secured.size(), out, diagnostic));
    CHECK(diagnostic.find("secured") != std::string::npos);

    std::vector<uint8_t> badVersion;
    badVersion.push_back(0x72);
    NetworkMessage out2;
    CHECK(!decodeNetworkMessage(&badVersion[0], badVersion.size(), out2, diagnostic));
}

int main()
{
    testGoldenBytes();
    testScalarRoundTrips();
    testPublisherIdTypes();
    testMultipleDataSetMessages();
    testTruncationSafety();
    testForeignHeaderFeatures();
    testKeepAliveDecode();
    testRejections();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
