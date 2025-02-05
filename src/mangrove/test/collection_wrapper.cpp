// Copyright 2016 MongoDB Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "catch.hpp"

#include <iostream>

#include <bsoncxx/builder/stream/document.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/pipeline.hpp>
#include <mongocxx/stdx.hpp>

#include <boson/bson_streambuf.hpp>
#include <mangrove/collection_wrapper.hpp>

using namespace bsoncxx;
using namespace mongocxx;
using namespace mangrove;

class Foo {
   public:
    int a, b, c;

    bool operator==(const Foo& rhs) {
        return (a == rhs.a) && (b == rhs.b) && (c == rhs.c);
    }

    template <class Archive>
    void serialize(Archive& ar) {
        ar(CEREAL_NVP(a), CEREAL_NVP(b), CEREAL_NVP(c));
    }
};

// Represents an aggregation result
class FooResult {
   public:
    int a;
    int sum;

    bool operator==(const FooResult& rhs) {
        return (a == rhs.a) && (sum == rhs.sum);
    }

    template <class Archive>
    void serialize(Archive& ar) {
        ar(CEREAL_NVP(a), CEREAL_NVP(sum));
    }
};

TEST_CASE(
    "collection_wrapper class wraps collection's CRUD interface, with automatic "
    "serialization.",
    "[mangrove::collection_wrapper]") {
    // set up test BSON documents and objects
    std::string json_str = R"({"a": 1, "b":4, "c": 9})";
    auto doc = from_json(json_str);
    auto doc_view = doc.view();

    std::string json_str_2 = R"({"a": 1, "b":4, "c": 900})";
    auto doc_2 = from_json(json_str_2);
    auto doc_2_view = doc_2.view();

    Foo obj{1, 4, 9};
    instance::current();
    client conn{uri{}};
    collection coll = conn["testdb"]["testcollection"];
    collection_wrapper<Foo> foo_coll(coll);

    // TODO test pipeline aggregation
    SECTION("Test aggregation", "[mangrove::collection_wrapper]") {
        coll.delete_many({});
        for (int i = 0; i < 10; i++) {
            coll.insert_one(doc_view);
        }
        FooResult result_obj{1, 140};

        // Set up aggregation query that sums up every field of each individual document.
        pipeline stages;
        builder::stream::document group_stage;
        group_stage << "_id"
                    << "$a"
                    << "a" << builder::stream::open_document << "$sum"
                    << "$a" << builder::stream::close_document << "b"
                    << builder::stream::open_document << "$sum"
                    << "$b" << builder::stream::close_document << "c"
                    << builder::stream::open_document << "$sum"
                    << "$c" << builder::stream::close_document;
        builder::stream::document project_stage;
        project_stage << "a"
                      << "$_id"
                      << "sum" << builder::stream::open_document << "$add"
                      << builder::stream::open_array << "$a"
                      << "$b"
                      << "$c" << builder::stream::close_array << builder::stream::close_document;
        stages.group(group_stage.view());
        stages.project(project_stage.view());

        auto cur = foo_coll.aggregate<FooResult>(stages);
        int i = 0;
        for (FooResult fr : cur) {
            i++;
            REQUIRE(fr.a == result_obj.a);
        }
        REQUIRE(i == 1);
    }

    // test find
    SECTION("Test find()", "[mangrove::collection_wrapper]") {
        coll.delete_many({});

        for (int i = 0; i < 5; i++) {
            coll.insert_one(doc_view);
            coll.insert_one(doc_2_view);
        }

        deserializing_cursor<Foo> cur = foo_coll.find(from_json(R"({"c": {"$gt": 100}})"));
        int i = 0;
        for (Foo f : cur) {
            REQUIRE(f.c > 100);
            i++;
        }
        REQUIRE(i == 5);
    }

    SECTION("Test find_one()", "[mangrove::collection_wrapper]") {
        coll.delete_many({});
        coll.insert_one(doc_view);

        mongocxx::stdx::optional<Foo> res = foo_coll.find_one(doc_view);
        REQUIRE(res);
        if (res) {
            Foo obj_test = res.value();
            REQUIRE(obj_test == obj);
        }
    }

    SECTION("Test find_one_and_delete()", "[mangrove::collection_wrapper]") {
        coll.delete_many({});
        coll.insert_one(doc_view);

        mongocxx::stdx::optional<Foo> res = foo_coll.find_one_and_delete(doc_view);
        REQUIRE(res);
        if (res) {
            Foo obj_test = res.value();
            REQUIRE(obj_test == obj);
            int count = coll.count_documents(doc_view);
            REQUIRE(count == 0);
        }
    }

    // Test find_one_and_replace
    // TODO test when document could not be found
    SECTION("Test find_one_and_replace()", "[mangrove::collection_wrapper]") {
        coll.delete_many({});
        Foo replacement{1, 4, 555};
        coll.insert_one(doc_view);

        mongocxx::stdx::optional<Foo> res = foo_coll.find_one_and_replace(doc_view, replacement);
        REQUIRE(res);
        if (res) {
            Foo obj_test = res.value();
            REQUIRE(obj_test == obj);
        }
    }

    SECTION("Test insert_one().", "[mangrove::collection_wrapper]") {
        coll.delete_many({});
        auto res = foo_coll.insert_one(obj);
        REQUIRE(res);
    }

    // Test insert_many()
    SECTION("Test insert_many().", "[mangrove::collection_wrapper]") {
        coll.delete_many({});
        std::vector<Foo> foo_vec;
        for (int i = 0; i < 5; i++) {
            foo_vec.push_back(Foo{0, 0, i});
        }

        SECTION("Test insert_many() with a container.", "[mangrove::collection_wrapper]") {
            auto res = foo_coll.insert_many(foo_vec);
            REQUIRE(res);
            if (res) {
                int count = res.value().inserted_count();
                REQUIRE(count == 5);
            }
        }

        SECTION("Test insert_many() with a range of two iterators.",
                "[mangrove::collection_wrapper]") {
            auto res = foo_coll.insert_many(foo_vec.begin(), foo_vec.end());
            REQUIRE(res);
            if (res) {
                int count = res.value().inserted_count();
                REQUIRE(count == 5);
            }
        }
    }

    SECTION("Test replace_one().", "[mangrove::collection_wrapper]") {
        coll.delete_many({});
        coll.insert_one(doc_view);
        Foo obj2{1, 4, 999};

        auto res = foo_coll.replace_one(doc_view, obj2);
        REQUIRE(res);
        if (res) {
            int c = res.value().modified_count();
            REQUIRE(c == 1);
        }
    }

    coll.delete_many({});
}
