/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * test_configuration.cpp
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

#include <FxConfiguration.h>

#include <cstdio>
#include <limits>
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

static Configuration valid()
{
    Configuration configuration;
    configuration.automationComponent = "ProcessCell";
    configuration.publisherIdType = PubSub::PublisherIdUInt16;
    configuration.publisherId = 91;

    FunctionalEntityConfig entity;
    entity.name = "control";

    OutputDatasetConfig output;
    output.name = "env";
    output.writerGroupId = 200;
    output.dataSetWriterId = 1;
    output.publishingIntervalMs = 100;
    DatasetField f1;
    f1.address = "FX1.temperature";
    output.fields.push_back(f1);
    DatasetField f2;
    f2.name = "count";
    f2.address = "FX1.counter";
    output.fields.push_back(f2);
    entity.outputs.push_back(output);

    InputDatasetConfig input;
    input.name = "setpoints";
    DatasetField f3;
    f3.address = "FX1.setpoint";
    input.fields.push_back(f3);
    entity.inputs.push_back(input);

    configuration.entities.push_back(entity);
    return configuration;
}

static std::string refusalOf(Configuration configuration)
{
    try
    {
        finalizeAndValidate(configuration);
        return "";
    }
    catch (const std::exception& e)
    {
        return e.what();
    }
}

static bool refusedWith(const Configuration& configuration, const char* fragment)
{
    const std::string what = refusalOf(configuration);
    if (what.empty())
        return false;
    return what.find(fragment) != std::string::npos;
}

int main()
{
    /* the valid configuration passes, and field names default from leaves */
    {
        Configuration c = valid();
        finalizeAndValidate(c);
        CHECK(c.entities[0].outputs[0].fields[0].name == "temperature");
        CHECK(c.entities[0].outputs[0].fields[1].name == "count");
        CHECK(c.entities[0].inputs[0].fields[0].name == "setpoint");
    }
    /* an address without dots defaults to itself */
    {
        Configuration c = valid();
        c.entities[0].inputs[0].fields[0].address = "bare";
        finalizeAndValidate(c);
        CHECK(c.entities[0].inputs[0].fields[0].name == "bare");
    }

    /* component-level refusals */
    {
        Configuration c = valid();
        c.automationComponent = "";
        CHECK(refusedWith(c, "automationComponent"));
    }
    {
        Configuration c = valid();
        c.publisherIdType = PubSub::PublisherIdByte;
        c.publisherId = 300;
        CHECK(refusedWith(c, "does not fit"));
    }
    {
        Configuration c = valid();
        c.publisherIdType = PubSub::PublisherIdByte;
        c.publisherId = 255;
        CHECK(refusalOf(c).empty());
    }
    {
        Configuration c = valid();
        c.entities.clear();
        CHECK(refusedWith(c, "no FunctionalEntity"));
    }

    /* entity-level refusals */
    {
        Configuration c = valid();
        c.entities.push_back(c.entities[0]);
        CHECK(refusedWith(c, "duplicate FunctionalEntity"));
    }
    {
        Configuration c = valid();
        c.entities[0].name = "";
        CHECK(refusedWith(c, "empty FunctionalEntity"));
    }
    {
        Configuration c = valid();
        c.entities[0].outputs.clear();
        c.entities[0].inputs.clear();
        CHECK(refusedWith(c, "declares no datasets"));
    }

    /* dataset-level refusals */
    {
        Configuration c = valid();
        c.entities[0].outputs.push_back(c.entities[0].outputs[0]);
        CHECK(refusedWith(c, "duplicate OutputDataset"));
    }
    {
        Configuration c = valid();
        OutputDatasetConfig second = c.entities[0].outputs[0];
        second.name = "env2";
        second.writerGroupId = 200; /* collides */
        c.entities[0].outputs.push_back(second);
        CHECK(refusedWith(c, "writerGroupId 200"));
    }
    {
        /* the writer-group rule spans entities */
        Configuration c = valid();
        FunctionalEntityConfig other = c.entities[0];
        other.name = "control2";
        c.entities.push_back(other);
        CHECK(refusedWith(c, "writerGroupId 200"));
    }
    {
        Configuration c = valid();
        c.entities[0].outputs[0].publishingIntervalMs = 0;
        CHECK(refusedWith(c, "must be positive"));
    }
    {
        /* xs:double admits INF — a cast of that to a duration is UB */
        Configuration c = valid();
        c.entities[0].outputs[0].publishingIntervalMs = std::numeric_limits<double>::infinity();
        CHECK(refusedWith(c, "one day"));
    }
    {
        Configuration c = valid();
        c.entities[0].outputs[0].publishingIntervalMs = 1e300;
        CHECK(refusedWith(c, "one day"));
    }
    {
        /* larger ids would round through the services' JSON numbers */
        Configuration c = valid();
        c.publisherIdType = PubSub::PublisherIdUInt64;
        c.publisherId = 9007199254740993ull; /* 2^53 + 1 */
        CHECK(refusedWith(c, "2^53"));
    }
    {
        Configuration c = valid();
        c.entities[0].inputs.push_back(c.entities[0].inputs[0]);
        CHECK(refusedWith(c, "duplicate InputDataset"));
    }
    /* same name in both directions is fine — role disambiguates */
    {
        Configuration c = valid();
        c.entities[0].inputs[0].name = "env";
        CHECK(refusalOf(c).empty());
    }

    /* field-level refusals */
    {
        Configuration c = valid();
        c.entities[0].outputs[0].fields.clear();
        CHECK(refusedWith(c, "has no Fields"));
    }
    {
        Configuration c = valid();
        c.entities[0].outputs[0].fields[0].address = "";
        CHECK(refusedWith(c, "empty address"));
    }
    {
        Configuration c = valid();
        c.entities[0].outputs[0].fields[1].name = "";
        c.entities[0].outputs[0].fields[1].address = "FX2.temperature";
        /* both leaves are now 'temperature' */
        CHECK(refusedWith(c, "two Fields named 'temperature'"));
    }
    {
        Configuration c = valid();
        c.entities[0].outputs[0].fields[0].address = "FX1.";
        CHECK(refusedWith(c, "empty name"));
    }

    std::printf("%s: %d checks, %d failure(s)\n",
                g_failures == 0 ? "PASS" : "FAIL", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
