// Copyright 2020 Redpanda Data, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

#include "pandaproxy/error.h"
#include "pandaproxy/json/rjson_util.h"
#include "pandaproxy/schema_registry/test/avro_payloads.h"
#include "pandaproxy/schema_registry/test/client_utils.h"
#include "pandaproxy/schema_registry/types.h"
#include "pandaproxy/test/pandaproxy_fixture.h"
#include "pandaproxy/test/utils.h"

#include <seastar/util/bool_class.hh>

#include <limits>
#include <optional>

namespace pp = pandaproxy;
namespace ppj = pp::json;
namespace pps = pp::schema_registry;

using make_values_unsigned = ss::bool_class<struct make_values_unsigned_tag>;

struct request {
    pps::canonical_schema schema;
    std::optional<pps::schema_id> id = std::nullopt;
    std::optional<pps::schema_version> version = std::nullopt;
    // If the below is set to true, then cast id and version to an unsigned int
    // This validates testing the unsigned handling path in JSON deserializing
    make_values_unsigned values_unsigned = make_values_unsigned::no;
};

template<typename Buffer>
void rjson_serialize(::json::iobuf_writer<Buffer>& w, const request& r) {
    w.StartObject();
    if (r.id.has_value()) {
        w.Key("id");
        if (r.values_unsigned) {
            ::json::rjson_serialize(w, static_cast<unsigned int>(r.id.value()));
        } else {
            ::json::rjson_serialize(w, r.id.value());
        }
    }
    if (r.version.has_value()) {
        w.Key("version");
        if (r.values_unsigned) {
            ::json::rjson_serialize(
              w, static_cast<unsigned int>(r.version.value()));
        } else {
            ::json::rjson_serialize(w, r.version.value());
        }
    }
    w.Key("schema");
    ::json::rjson_serialize(w, r.schema.def().raw());
    if (r.schema.type() != pps::schema_type::avro) {
        w.Key("schemaType");
        ::json::rjson_serialize(w, to_string_view(r.schema.type()));
    }
    if (!r.schema.def().refs().empty()) {
        w.Key("references");
        w.StartArray();
        for (const auto& ref : r.schema.def().refs()) {
            w.StartObject();
            w.Key("name");
            ::json::rjson_serialize(w, ref.name);
            w.Key("subject");
            ::json::rjson_serialize(w, ref.sub);
            w.Key("version");
            ::json::rjson_serialize(w, ref.version);
            w.EndObject();
        }
        w.EndArray();
    }
    w.EndObject();
}

FIXTURE_TEST(
  schema_registry_post_subjects_subject_version, pandaproxy_test_fixture) {
    using namespace std::chrono_literals;

    info("Connecting client");
    auto client = make_schema_reg_client();

    {
        info("Post a schema as key (expect schema_id=1)");
        auto res = post_schema(
          client, pps::subject{"test-key"}, avro_int_payload);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);
        BOOST_REQUIRE_EQUAL(res.body, R"({"id":1})");
        BOOST_REQUIRE_EQUAL(
          res.headers.at(boost::beast::http::field::content_type),
          to_header_value(ppj::serialization_format::schema_registry_v1_json));
    }

    {
        info("Repost a schema as key (expect schema_id=1)");
        auto res = post_schema(
          client, pps::subject{"test-key"}, avro_int_payload);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);
        BOOST_REQUIRE_EQUAL(res.body, R"({"id":1})");
        BOOST_REQUIRE_EQUAL(
          res.headers.at(boost::beast::http::field::content_type),
          to_header_value(ppj::serialization_format::schema_registry_v1_json));
    }

    {
        info("Repost a schema as value (expect schema_id=1)");
        auto res = post_schema(
          client, pps::subject{"test-value"}, avro_int_payload);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);
        BOOST_REQUIRE_EQUAL(res.body, R"({"id":1})");
        BOOST_REQUIRE_EQUAL(
          res.headers.at(boost::beast::http::field::content_type),
          to_header_value(ppj::serialization_format::schema_registry_v1_json));
    }

    {
        info("Post a new schema as key (expect schema_id=2)");
        auto res = post_schema(
          client, pps::subject{"test-key"}, avro_long_payload);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);
        BOOST_REQUIRE_EQUAL(res.body, R"({"id":2})");
        BOOST_REQUIRE_EQUAL(
          res.headers.at(boost::beast::http::field::content_type),
          to_header_value(ppj::serialization_format::schema_registry_v1_json));
    }
}

FIXTURE_TEST(
  schema_registry_post_subjects_subject_version_with_id,
  pandaproxy_test_fixture) {
    using namespace std::chrono_literals;

    info("Connecting client");
    auto client = make_schema_reg_client();

    const ss::sstring schema_2{
      R"({
  "schema": "\"string\"",
  "id": 2,
  "schemaType": "AVRO"
})"};

    const ss::sstring schema_2_as_4{
      R"({
  "schema": "\"string\"",
  "id": 4,
  "schemaType": "AVRO"
})"};

    const ss::sstring schema_4{
      R"({
  "schema": "\"int\"",
  "id": 4,
  "schemaType": "AVRO"
})"};

    const ss::sstring schema_4_as_2{
      R"({
  "schema": "\"int\"",
  "id": 2,
  "schemaType": "AVRO"
})"};

    const pps::subject subject{"test-key"};
    put_config(client, subject, pps::compatibility_level::none);

    {
        info("Post schema 4 as key with id 4 (higher than next_id)");
        auto res = post_schema(client, subject, schema_4);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);
        BOOST_REQUIRE_EQUAL(res.body, R"({"id":4})");

        res = get_subject_versions(client, subject);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        std::vector<pps::schema_version> expected{pps::schema_version{1}};
        auto versions = get_body_versions(res.body);
        BOOST_REQUIRE_EQUAL(versions, expected);
    }

    {
        info("Post schema 4 as key with id 2 (expect error 42205)");
        auto res = post_schema(client, subject, schema_2_as_4);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(),
          boost::beast::http::status::unprocessable_entity);
        auto eb = get_error_body(res.body);
        BOOST_REQUIRE(
          eb.ec
          == pp::reply_error_code::subject_version_schema_id_already_exists);
        BOOST_REQUIRE_EQUAL(
          eb.message, R"(Overwrite new schema with id 4 is not permitted.)");
    }

    {
        info("Post schema 2 as key with id 2 (lower than next id)");
        auto res = post_schema(client, subject, schema_2);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);
        BOOST_REQUIRE_EQUAL(res.body, R"({"id":2})");

        res = get_subject_versions(client, subject);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        std::vector<pps::schema_version> expected{
          pps::schema_version{1}, pps::schema_version{2}};
        auto versions = get_body_versions(res.body);
        BOOST_REQUIRE_EQUAL(versions, expected);
    }

    {
        info("Post schema 4 as key with id 2 (expect error 42207)");
        auto res = post_schema(client, subject, schema_4_as_2);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(),
          boost::beast::http::status::unprocessable_entity);
        auto eb = get_error_body(res.body);
        BOOST_REQUIRE(
          eb.ec
          == pp::reply_error_code::subject_version_schema_id_already_exists);
        BOOST_REQUIRE_EQUAL(
          eb.message,
          R"(Schema already registered with id 4 instead of input id 2)");
    }
}

FIXTURE_TEST(
  schema_registry_post_subjects_subject_version_with_version_no_compat,
  pandaproxy_test_fixture) {
    using namespace std::chrono_literals;

    info("Connecting client");
    auto client = make_schema_reg_client();

    const ss::sstring schema_string_v2{
      R"({
  "schema": "\"string\"",
  "version": 2,
  "schemaType": "AVRO"
})"};

    const ss::sstring schema_int_v2{
      R"({
  "schema": "\"int\"",
  "version": 2,
  "schemaType": "AVRO"
})"};

    const ss::sstring schema_int_v3{
      R"({
  "schema": "\"int\"",
  "version": 3,
  "schemaType": "AVRO"
})"};

    const pps::subject subject{"test-key"};
    put_config(client, subject, pps::compatibility_level::none);

    {
        info("Post schema as v2 (higher than next_version)");
        auto res = post_schema(client, subject, schema_string_v2);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);
        BOOST_REQUIRE_EQUAL(res.body, R"({"id":1})");

        res = get_subject_versions(client, subject);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        std::vector<pps::schema_version> expected{pps::schema_version{2}};
        auto versions = get_body_versions(res.body);
        BOOST_REQUIRE_EQUAL(versions, expected);
    }

    {
        info("Repost schema v2 (expect success, existing schema id)");
        auto res = post_schema(client, subject, schema_string_v2);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);
        BOOST_REQUIRE_EQUAL(res.body, R"({"id":1})");

        res = get_subject_versions(client, subject);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        std::vector<pps::schema_version> expected{pps::schema_version{2}};
        auto versions = get_body_versions(res.body);
        BOOST_REQUIRE_EQUAL(versions, expected);
    }

    {
        info("Overwrite schema v2 (expect success, new schema id)");
        auto res = post_schema(client, subject, schema_int_v2);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);
        BOOST_REQUIRE_EQUAL(res.body, R"({"id":2})");

        res = get_subject_versions(client, subject);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        std::vector<pps::schema_version> expected{pps::schema_version{2}};
        auto versions = get_body_versions(res.body);
        BOOST_REQUIRE_EQUAL(versions, expected);

        res = http_request(
          client,
          fmt::format("/subjects/{}/versions/2/schema", subject()),
          boost::beast::http::verb::get,
          ppj::serialization_format::schema_registry_v1_json,
          ppj::serialization_format::schema_registry_v1_json);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);
        BOOST_REQUIRE_EQUAL(res.body, R"("int")");
    }

    {
        info("Post existing schema as v3 (expect existing schema id, no new "
             "version)");
        auto res = post_schema(client, subject, schema_int_v3);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);
        BOOST_REQUIRE_EQUAL(res.body, R"({"id":2})");

        res = get_subject_versions(client, subject);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        std::vector<pps::schema_version> expected{pps::schema_version{2}};
        auto versions = get_body_versions(res.body);
        BOOST_REQUIRE_EQUAL(versions, expected);
    }
}

FIXTURE_TEST(
  schema_registry_post_subjects_subject_version_with_version_with_compat,
  pandaproxy_test_fixture) {
    using namespace std::chrono_literals;

    info("Connecting client");
    auto client = make_schema_reg_client();

    const ss::sstring schema_v2{
      R"({
  "schema": "{\"type\": \"record\", \"name\": \"r1\", \"fields\" : [{\"name\": \"f1\", \"type\": \"string\"}]}",
  "version": 2,
  "schemaType": "AVRO"
})"};

    const ss::sstring schema_v3_no_default{
      R"({
  "schema": "{\"type\": \"record\", \"name\": \"r1\", \"fields\" : [{\"name\": \"f1\", \"type\": \"string\"}, {\"name\": \"f2\", \"type\": \"string\"}]}",
  "version": 3,
  "schemaType": "AVRO"
})"};

    const ss::sstring schema_v3{
      R"({
  "schema": "{\"type\": \"record\", \"name\": \"r1\", \"fields\" : [{\"name\": \"f1\", \"type\": \"string\"}, {\"name\": \"f2\", \"type\": \"string\", \"default\": \"f2\"}]}",
  "version": 3,
  "schemaType": "AVRO"
})"};

    pps::subject subject{"in-version-order"};
    put_config(client, subject, pps::compatibility_level::backward);

    {
        info("Post schema v2");
        auto res = post_schema(client, subject, schema_v2);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        res = get_subject_versions(client, subject);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        std::vector<pps::schema_version> expected{pps::schema_version{2}};
        auto versions = get_body_versions(res.body);
        BOOST_REQUIRE_EQUAL(versions, expected);
    }

    {
        info("Post schema v3_no_default (expect conflict)");
        auto res = post_schema(client, subject, schema_v3_no_default);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::conflict);
    }

    {
        info("Post schema v3");
        auto res = post_schema(client, subject, schema_v3);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        res = get_subject_versions(client, subject);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        std::vector<pps::schema_version> expected{
          pps::schema_version{2}, pps::schema_version{3}};
        auto versions = get_body_versions(res.body);
        BOOST_REQUIRE_EQUAL(versions, expected);
    }

    subject = pps::subject{"in-reverse-order"};
    put_config(client, subject, pps::compatibility_level::backward);

    {
        info("Post schema v3_no_default");
        auto res = post_schema(client, subject, schema_v3_no_default);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        res = get_subject_versions(client, subject);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        std::vector<pps::schema_version> expected{pps::schema_version{3}};
        auto versions = get_body_versions(res.body);
        BOOST_REQUIRE_EQUAL(versions, expected);
    }

    {
        info("Post schema v2 (expect success, despite incompatibility)");
        auto res = post_schema(client, subject, schema_v2);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        res = get_subject_versions(client, subject);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        std::vector<pps::schema_version> expected{
          pps::schema_version{2}, pps::schema_version{3}};
        auto versions = get_body_versions(res.body);
        BOOST_REQUIRE_EQUAL(versions, expected);
    }

    {
        info("Post schema v3 (expect success)");
        auto res = post_schema(client, subject, schema_v3);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        res = get_subject_versions(client, subject);
        BOOST_REQUIRE_EQUAL(
          res.headers.result(), boost::beast::http::status::ok);

        std::vector<pps::schema_version> expected{
          pps::schema_version{2}, pps::schema_version{3}};
        auto versions = get_body_versions(res.body);
        BOOST_REQUIRE_EQUAL(versions, expected);
    }
}

FIXTURE_TEST(
  schema_registry_post_subjects_subject_version_invalid_payload,
  pandaproxy_test_fixture) {
    using namespace std::chrono_literals;

    info("Connecting client");
    auto client = make_schema_reg_client();

    {
        info("Post an invalid payload");
        auto res = post_schema(client, pps::subject{"test-key"}, R"(
{
  "schema": "\"int\"",
  "InvalidField": "AVRO"
})");

        BOOST_REQUIRE_EQUAL(
          res.headers.result(),
          boost::beast::http::status::unprocessable_entity);
        BOOST_REQUIRE(std::string_view(res.body).starts_with(
          R"({"error_code":422,"message":")"));
    }
}

FIXTURE_TEST(
  schema_registry_post_subjects_subject_version_invalid_schema,
  pandaproxy_test_fixture) {
    using namespace std::chrono_literals;

    info("Connecting client");
    auto client = make_schema_reg_client();

    {
        info("Post an invalid schema");
        auto res = post_schema(client, pps::subject{"test-key"}, R"(
{
  "schema": "not_json",
  "schemaType": "AVRO"
})");

        BOOST_REQUIRE_EQUAL(
          res.headers.result(),
          boost::beast::http::status::unprocessable_entity);
        BOOST_REQUIRE(std::string_view(res.body).starts_with(
          R"({"error_code":422,"message":")"));
    }
}

FIXTURE_TEST(
  schema_registry_post_subjects_subject_version_many_proto,
  pandaproxy_test_fixture) {
    using namespace std::chrono_literals;

    info("Connecting client");
    auto client = make_schema_reg_client();

    for (auto i = 0; i < 10; ++i) {
        info("Post a schema as key (expect schema_id=1)");
        auto res = post_schema(client, pps::subject{"test-key"}, R"(
{
  "schema": "syntax = \"proto3\"; message Simple { string i = 1; }",
  "schemaType": "PROTOBUF"
})");
        BOOST_REQUIRE_EQUAL(res.body, R"({"id":1})");
        BOOST_REQUIRE_EQUAL(
          res.headers.at(boost::beast::http::field::content_type),
          to_header_value(ppj::serialization_format::schema_registry_v1_json));
    }
}

FIXTURE_TEST(schema_registry_post_avro_references, pandaproxy_test_fixture) {
    using namespace std::chrono_literals;

    const auto company_req = request{pps::canonical_schema{
      pps::subject{"company-value"},
      pps::canonical_schema_definition(
        R"({
  "namespace": "com.redpanda",
  "type": "record",
  "name": "company",
  "fields": [
    {
      "name": "id",
      "type": "string"
    },
    {
      "name": "name",
      "type": "string"
    },
    {
      "name": "address",
      "type": "string"
    }
  ]
})",
        pps::schema_type::avro)}};

    const auto employee_req = request{pps::canonical_schema{
      pps::subject{"employee-value"},
      pps::canonical_schema_definition{
        R"({
  "namespace": "com.redpanda",
  "type": "record",
  "name": "employee",
  "fields": [
    {
      "name": "id",
      "type": "string"
    },
    {
      "name": "name",
      "type": "string"
    },
    {
      "name": "company",
      "type": "com.redpanda.company"
    }
  ]
})",
        pps::schema_type::avro,
        {{"com.redpanda.company",
          pps::subject{"company-value"},
          pps::schema_version{1}}}}}};

    info("Connecting client");
    auto client = make_schema_reg_client();

    info("Post company schema (expect schema_id=1)");
    auto res = post_schema(
      client, company_req.schema.sub(), ppj::rjson_serialize_str(company_req));
    BOOST_REQUIRE_EQUAL(res.body, R"({"id":1})");
    BOOST_REQUIRE_EQUAL(
      res.headers.at(boost::beast::http::field::content_type),
      to_header_value(ppj::serialization_format::schema_registry_v1_json));

    info("Post employee schema (expect schema_id=2)");
    res = post_schema(
      client,
      employee_req.schema.sub(),
      ppj::rjson_serialize_str(employee_req));
    BOOST_REQUIRE_EQUAL(res.body, R"({"id":2})");
    BOOST_REQUIRE_EQUAL(
      res.headers.at(boost::beast::http::field::content_type),
      to_header_value(ppj::serialization_format::schema_registry_v1_json));
}

FIXTURE_TEST(
  schema_registry_post_subjects_subject_id_smaller_than_one,
  pandaproxy_test_fixture) {
    using namespace std::chrono_literals;

    const auto simple_req_id_0 = request{
      pps::canonical_schema{
        pps::subject{"simple-value"},
        pps::canonical_schema_definition(
          R"({
"namespace": "com.redpanda",
  "type": "record",
  "name": "employee",
  "fields": [
    {
      "name": "id",
      "type": "string"
    }
]
  })",
          pps::schema_type::avro)},
      pps::schema_id{0}};

    const auto simple_req_id_minus_1 = request{
      pps::canonical_schema{
        pps::subject{"simple-value"},
        pps::canonical_schema_definition(
          R"({
"namespace": "com.redpanda",
  "type": "record",
  "name": "employee",
  "fields": [
    {
      "name": "id",
      "type": "string"
    },
    {
      "name": "id2",
      "type": "string",
      "default": ""
    }
]
  })",
          pps::schema_type::avro)},
      pps::schema_id{-1}};

    info("Connecting client");
    auto client = make_schema_reg_client();
    info("Post simple schema (expect schema_id=0)");
    auto res = post_schema(
      client,
      simple_req_id_0.schema.sub(),
      ppj::rjson_serialize_str(simple_req_id_0));
    BOOST_REQUIRE_EQUAL(res.body, R"({"id":0})");
    BOOST_REQUIRE_EQUAL(
      res.headers.at(boost::beast::http::field::content_type),
      to_header_value(ppj::serialization_format::schema_registry_v1_json));

    info("Post simple schema with id=-1 (expect schema_id=1)");
    res = post_schema(
      client,
      simple_req_id_minus_1.schema.sub(),
      ppj::rjson_serialize_str(simple_req_id_minus_1));
    BOOST_REQUIRE_EQUAL(res.body, R"({"id":1})");
    BOOST_REQUIRE_EQUAL(
      res.headers.at(boost::beast::http::field::content_type),
      to_header_value(ppj::serialization_format::schema_registry_v1_json));
}

FIXTURE_TEST(
  schema_registry_post_subjects_subject_id_too_big, pandaproxy_test_fixture) {
    using namespace std::chrono_literals;

    const auto simple_req_id_invalid = request{
      pps::canonical_schema{
        pps::subject{"simple-value"},
        pps::canonical_schema_definition(
          R"({
"namespace": "com.redpanda",
  "type": "record",
  "name": "employee",
  "fields": [
    {
      "name": "id",
      "type": "string"
    }
]
  })",
          pps::schema_type::avro)},
      pps::schema_id{std::numeric_limits<int>::min()},
      std::nullopt,
      make_values_unsigned::yes};

    info("Connecting client");
    auto client = make_schema_reg_client();
    info("Post simple schema (expect error)");
    auto res = post_schema(
      client,
      simple_req_id_invalid.schema.sub(),
      ppj::rjson_serialize_str(simple_req_id_invalid));
    BOOST_REQUIRE_EQUAL(
      res.headers.result(), boost::beast::http::status::unprocessable_entity);
    BOOST_REQUIRE(std::string_view(res.body).starts_with(
      R"({"error_code":422,"message":")"));
}

FIXTURE_TEST(
  schema_registry_post_subjects_version_zero, pandaproxy_test_fixture) {
    const auto simple_req_first = request{
      pps::canonical_schema{
        pps::subject{"simple-value"},
        pps::canonical_schema_definition(
          R"({
"namespace": "com.redpanda",
  "type": "record",
  "name": "employee",
  "fields": [
    {
      "name": "id",
      "type": "string"
    }
]
  })",
          pps::schema_type::avro)},
      std::nullopt,
      pps::schema_version{0}};

    const auto simple_req_second = request{
      pps::canonical_schema{
        pps::subject{"simple-value"},
        pps::canonical_schema_definition(
          R"({
"namespace": "com.redpanda",
  "type": "record",
  "name": "employee",
  "fields": [
    {
      "name": "id",
      "type": "string"
    },
    {
      "name": "id2",
      "type": "string",
      "default": ""
    }
]
  })",
          pps::schema_type::avro)},
      std::nullopt,
      pps::schema_version{0}};

    info("Connecting client");
    auto client = make_schema_reg_client();
    info("Post simple schema first (expect schema_id=1)");
    auto res = post_schema(
      client,
      simple_req_first.schema.sub(),
      ppj::rjson_serialize_str(simple_req_first));
    BOOST_REQUIRE_EQUAL(res.body, R"({"id":1})");
    BOOST_REQUIRE_EQUAL(
      res.headers.at(boost::beast::http::field::content_type),
      to_header_value(ppj::serialization_format::schema_registry_v1_json));

    info("Post simple schema second (expect schema_id=2)");
    res = post_schema(
      client,
      simple_req_second.schema.sub(),
      ppj::rjson_serialize_str(simple_req_second));
    BOOST_REQUIRE_EQUAL(res.body, R"({"id":2})");
    BOOST_REQUIRE_EQUAL(
      res.headers.at(boost::beast::http::field::content_type),
      to_header_value(ppj::serialization_format::schema_registry_v1_json));

    info("Getting versions, expect ([1,2])");
    res = get_subject_versions(client, pps::subject{"simple-value"});
    BOOST_REQUIRE_EQUAL(res.headers.result(), boost::beast::http::status::ok);
    BOOST_REQUIRE_EQUAL(res.body, R"([1,2])");
    BOOST_REQUIRE_EQUAL(
      res.headers.at(boost::beast::http::field::content_type),
      to_header_value(ppj::serialization_format::schema_registry_v1_json));
}
FIXTURE_TEST(
  schema_registry_post_subjects_version_exhausted, pandaproxy_test_fixture) {
    const auto simple_req_first = request{
      pps::canonical_schema{
        pps::subject{"simple-value"},
        pps::canonical_schema_definition(
          R"({
"namespace": "com.redpanda",
  "type": "record",
  "name": "employee",
  "fields": [
    {
      "name": "id",
      "type": "string"
    }
]
  })",
          pps::schema_type::avro)},
      std::nullopt,
      pps::schema_version{std::numeric_limits<int>::max()}};

    const auto simple_req_second = request{
      pps::canonical_schema{
        pps::subject{"simple-value"},
        pps::canonical_schema_definition(
          R"({
"namespace": "com.redpanda",
  "type": "record",
  "name": "employee",
  "fields": [
    {
      "name": "id",
      "type": "string"
    },
    {
      "name": "id2",
      "type": "string",
      "default": ""
    }
]
  })",
          pps::schema_type::avro)},
      std::nullopt,
      pps::schema_version{0}};
    info("Connecting client");
    auto client = make_schema_reg_client();
    info("Post simple schema first (expect schema_id=1)");
    auto res = post_schema(
      client,
      simple_req_first.schema.sub(),
      ppj::rjson_serialize_str(simple_req_first));
    BOOST_REQUIRE_EQUAL(res.body, R"({"id":1})");
    BOOST_REQUIRE_EQUAL(
      res.headers.at(boost::beast::http::field::content_type),
      to_header_value(ppj::serialization_format::schema_registry_v1_json));

    info("Post simple schema second (expect error)");
    res = post_schema(
      client,
      simple_req_second.schema.sub(),
      ppj::rjson_serialize_str(simple_req_second));
    BOOST_REQUIRE_EQUAL(
      res.headers.result(), boost::beast::http::status::internal_server_error);
    BOOST_REQUIRE(std::string_view(res.body).starts_with(
      R"({"error_code":500,"message":")"));
}
