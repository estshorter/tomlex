// clang-format off
#include "pch.h"

#include <tomlex/tomlex.hpp>
#include <tomlex/resolvers.hpp>
// clang-format on

using std::string;
using tomlex::register_resolver;
using tomlex::detail::find_from_root;
using namespace toml::literals::toml_literals;

char* filename_good;
char* filename_bad;

toml::value no_op(toml::value const& args) { return args; };
toml::value lt() { return toml::local_time(std::chrono::hours(8) + std::chrono::minutes(10)); };

toml::value add(toml::value&& args) {
	auto& array_ = args.as_array();
	std::int64_t ret = 0;
	for (auto& item : array_) {
		auto val = item.as_integer();
		ret += val;
	}
	return ret;
}

toml::value join(toml::value&& args, std::string const& sep = "_") {
	switch (args.type()) {
		case toml::value_t::array: {
			auto& array_ = args.as_array();
			std::ostringstream oss;
			for (decltype(array_.size()) i = 0; i < array_.size() - 1; i++) {
				oss << tomlex::detail::to_string(array_[i]) << sep;
			}
			oss << tomlex::detail::to_string(array_[array_.size() - 1]);
			return oss.str();
		}
		default:
			return args;
	}
}

class TestEnvironment : public ::testing::Environment {
   public:
	virtual void SetUp() {
		register_resolver("add", add);
		register_resolver("concat", [](auto&& args) { return join(std::move(args), ""); });
		register_resolver("join", [](auto&& args) { return join(std::move(args)); });
		register_resolver("no_op", no_op);
		register_resolver("env", tomlex::resolvers::env<>);
		register_resolver("decode", tomlex::resolvers::decode<>);
		register_resolver("lt", lt);
	}
};

class TesttomlextGoodTest : public ::testing::Test {
   public:
	static void SetUpTestCase() {
		// std::cout << "good toml file: " << filename_good << std::endl;
		std::string filename = filename_good;
		cfg = toml::parse(filename);
	}
	static inline toml::value cfg;
};

class TesttomlextBadTest : public ::testing::Test {
   public:
	// データメンバーの初期化
	virtual void SetUp() {
		// std::cout << "bad toml file: " << filename_bad << std::endl;
		std::string filename = filename_bad;
		cfg = toml::parse(filename);
	}
	static inline toml::value cfg;
};

TEST_F(TesttomlextGoodTest, interp) {
	auto result = find_from_root(cfg, "interp1");
	EXPECT_EQ(result.as_string(), "estshorter");
	result = find_from_root(cfg, "interp2");
	EXPECT_EQ(result.as_string(), "estshorter");
}

TEST_F(TesttomlextGoodTest, resolver_type) {
	auto result = find_from_root(cfg, "resolver1");
	EXPECT_EQ(result.as_integer(), 3);
	result = find_from_root(cfg, "resolver2");
	EXPECT_EQ(result.as_string(), "ab  ");
	result = find_from_root(cfg, "resolver3");
	EXPECT_EQ(result.as_string(), "ab");
	result = find_from_root(cfg, "resolver4");
	EXPECT_EQ(result.as_string(), "^ab/c%");
	result = find_from_root(cfg, "resolver5");
	EXPECT_EQ(tomlex::detail::to_string(result), "08:10:00");
}

TEST_F(TesttomlextGoodTest, resolver_interp) {
	auto result = find_from_root(cfg, "resolver_interp1");
	EXPECT_EQ(result.as_integer(), 2);
	result = find_from_root(cfg, "resolver_interp2");
	EXPECT_EQ(result.as_integer(), 2);
	result = find_from_root(cfg, "resolver_interp3");
	EXPECT_EQ(result.as_integer(), 12);
}

TEST_F(TesttomlextGoodTest, raw_string) {
	auto result = find_from_root(cfg, "raw_string1");
	EXPECT_EQ(result.as_string(), "&{");
	result = find_from_root(cfg, "raw_string2");
	EXPECT_EQ(result.as_string(), "{hogehoge}");
	result = find_from_root(cfg, "raw_string3");
	EXPECT_EQ(result.as_string(), "[hogehoge]");
	result = find_from_root(cfg, "raw_string4");
	EXPECT_EQ(result.as_string(), "[[hogehoge]]");
}

TEST_F(TesttomlextGoodTest, array_) {
	auto result = find_from_root(cfg, "arr");
	EXPECT_TRUE(result.is_array());
	EXPECT_EQ(tomlex::detail::to_string(result), "[0,1,2]");
	result = find_from_root(cfg, "arr_joined");
	EXPECT_EQ(result.as_string(), "0_1_2");
	result = find_from_root(cfg, "arr_interp");
	EXPECT_TRUE(result.is_array());
	EXPECT_EQ(tomlex::detail::to_string(result), "[0,1,2]");
	result = find_from_root(cfg, "arr_cat");
	EXPECT_EQ(result.as_string(), "[0,1,2]a");
	result = find_from_root(cfg, "arr_str_");
	EXPECT_EQ(result.as_string(), "[0,1,2]");
	result = find_from_root(cfg, "arr_str_2");
	EXPECT_TRUE(result.is_string());
	EXPECT_EQ(tomlex::detail::to_string(result), "[0,1,2]");
}

TEST_F(TesttomlextGoodTest, array_of_array) {
	auto result = find_from_root(cfg, "arrarr");
	EXPECT_TRUE(result.is_array());
	EXPECT_EQ(tomlex::detail::to_string(result), "[[0,1],[2,3]]");
	result = find_from_root(cfg, "arrarr_");
	EXPECT_TRUE(result.is_array());
	EXPECT_EQ(tomlex::detail::to_string(result), "[[0,1],[2,3]]");
}

TEST_F(TesttomlextGoodTest, table) {
	auto result = find_from_root(cfg, "table_");
	EXPECT_EQ(result.as_table(), "{x=1,y=2}"_toml.as_table());
	result = find_from_root(cfg, "table_test");
	EXPECT_EQ(result.as_table(), "{x=1,y=2}"_toml.as_table());
	result = find_from_root(cfg, "table_cat");
	auto table_str = tomlex::detail::to_string("{x=1,y=2}"_toml);
	EXPECT_EQ(result.as_string(), table_str + "1");
}

TEST_F(TesttomlextGoodTest, bool_) {
	auto result = find_from_root(cfg, "bool_");
	EXPECT_TRUE(result.is_boolean());
	EXPECT_EQ(tomlex::detail::to_string(result), "true");
	result = find_from_root(cfg, "bool_cat");
	EXPECT_EQ(result.as_string(), "trueA");
}

TEST_F(TesttomlextGoodTest, integer) {
	auto result = find_from_root(cfg, "float_cat");
	EXPECT_EQ(result.as_string(), "11.0A");
	result = find_from_root(cfg, "nan_cat");
	EXPECT_EQ(result.as_string(), "nanA");
	result = find_from_root(cfg, "nan_");
	EXPECT_TRUE(std::isnan(result.as_floating()));
	result = find_from_root(cfg, "inf_cat");
	EXPECT_EQ(result.as_string(), "infH");
	result = find_from_root(cfg, "inf_");
	EXPECT_TRUE(std::isinf(result.as_floating()));
}

TEST_F(TesttomlextGoodTest, float) {
	auto result = find_from_root(cfg, "int_cat");
	EXPECT_EQ(result.as_string(), "10 A");
}

TEST_F(TesttomlextGoodTest, datetime) {
	auto result = find_from_root(cfg, "date_");
	ASSERT_EQ(tomlex::detail::to_string(result),
			  "[1979-05-27T00:32:00.999999-07:00,1979-05-27T07:32:00,1979-05-27,07:32:00]");
	result = find_from_root(cfg, "ld_cat");
	EXPECT_EQ(result.as_string(), "1979-05-27 a");
	result = find_from_root(cfg, "lt_cat");
	EXPECT_EQ(result.as_string(), "07:32:00 a");
	result = find_from_root(cfg, "ldt_cat");
	EXPECT_EQ(result.as_string(), "1979-05-27T07:32:00 a");
	result = find_from_root(cfg, "oft_cat");
	EXPECT_EQ(result.as_string(), "1979-05-27T00:32:00.999999-07:00 a");
}

TEST_F(TesttomlextGoodTest, find) {
	auto result = find_from_root(cfg, "owner", "name");
	EXPECT_EQ(result.as_string(), "estshorter");
	const auto& owner = toml::find(cfg, "owner");
	result = tomlex::detail::find(cfg, owner, "name");
	EXPECT_EQ(result.as_string(), "estshorter");
}

TEST_F(TesttomlextBadTest, bad) {
	EXPECT_THROW(find_from_root(cfg, "empty_throw"), std::runtime_error);
	EXPECT_THROW(find_from_root(cfg, "circular1"), std::runtime_error);
}

TEST(TesttomlextTest, resolve) {
	auto cfg = R"(d='${no_op:["${concat: ["A","B","C"]}", "D"]}')"_toml;
	cfg = tomlex::resolve(std::move(cfg));
	ASSERT_EQ(tomlex::detail::to_string(cfg), R"({d=[ABC,D]})");
}

TEST(TesttomlextTest, from_cli) {
	constexpr char const* const keys[] = {"job_id  =   'hoge'", "a.b.c.d  =  120", "float=1.2"};
	auto cfg = tomlex::from_cli(3, keys, 0).as_table();
	auto expect = R"(job_id='hoge'
a={b={c={d=120}}}
float=1.2)"_toml.as_table();
	ASSERT_EQ(cfg, expect);
	ASSERT_THROW(tomlex::from_cli(3, keys, 3).as_table(), std::runtime_error);

	constexpr char const* const keys2[] = {"10"};
	ASSERT_THROW(tomlex::from_cli(1, keys2, 0).as_table(), std::runtime_error);
}

TEST(TesttomlextTest, merge) {
	auto base = R"(val=1)"_toml.as_table();
	ASSERT_EQ(base["val"].as_integer(), 1);
	auto modified = R"(val=1000)"_toml.as_table();
	ASSERT_EQ(modified["val"].as_integer(), 1000);

	base.merge(modified);
	ASSERT_EQ(base["val"].as_integer(), 1);

	modified.merge(std::move(base));
	ASSERT_EQ(modified["val"].as_integer(), 1000);
}

TEST(TesttomlextTest, clear_resolver) {
	std::string resolver_name = "__no_op__";
	register_resolver(resolver_name, no_op);

	auto it = tomlex::resolver_table<>.find(resolver_name);
	ASSERT_NE(it, tomlex::resolver_table<>.end());
	tomlex::clear_resolver(resolver_name);
	it = tomlex::resolver_table<>.find(resolver_name);
	ASSERT_EQ(it, tomlex::resolver_table<>.end());
	ASSERT_THROW(tomlex::clear_resolver(resolver_name), std::runtime_error);
}

int main(int argc, char* argv[]) {
	::testing::InitGoogleTest(&argc, argv);
	filename_good = argv[1];
	filename_bad = argv[2];

	::testing::AddGlobalTestEnvironment(new TestEnvironment);
	return RUN_ALL_TESTS();
}