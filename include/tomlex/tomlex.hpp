#pragma once

#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>
#include <toml.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tomlex {
namespace utils {
// https://stackoverflow.com/questions/3418231/replace-part-of-a-string-with-another-string
void replace_all(std::string& str, const std::string& from, const std::string& to) {
	if (from.empty()) return;
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length();  // In case 'to' contains 'from', like replacing 'x' with 'yx'
	}
}

inline std::vector<std::string> split(const std::string& str, const char delim) {
	if (str.empty()) {
		return {""};
	}

	std::vector<std::string> res;
	std::istringstream ss(str);
	std::string buffer;
	while (std::getline(ss, buffer, delim)) {
		res.push_back(buffer);
	}
	if (str[str.length() - 1] == delim) {
		res.push_back("");
	}
	return res;
}

constexpr auto ws = " \t\n\r\f\v";

inline std::string_view rtrim(std::string_view s, const char* t = ws) {
	auto pos = s.find_last_not_of(t);
	if (pos == std::string::npos) {
		s.remove_suffix(s.size());
	} else {
		s.remove_suffix(s.size() - pos - 1);
	}
	return s;
}

inline std::string_view ltrim(std::string_view s, const char* t = ws) {
	auto pos = std::min(s.find_first_not_of(t), s.size());
	s.remove_prefix(pos);
	return s;
}

inline std::string_view trim(std::string_view s, const char* t = ws) {
	return ltrim(rtrim(s, t), t);
}

}  // namespace utils

// foward decl
namespace detail {
template <typename Value>
void resolve_impl(Value& val, Value const& root_, std::unordered_set<std::string>& interpolating_);
template <typename Value>
Value parse_toml_literal(toml::detail::location loc);
}  // namespace detail

template <typename Value = toml::value>
using resolver_type = std::function<Value(Value&&)>;

template <typename Value = toml::value>
static inline std::unordered_map<std::string, resolver_type<Value>> resolver_table;

// decay_tしないとうまくオーバーロード解決できない
template <typename Value = toml::value>
void register_resolver(std::string const& resolver_name,
					   std::decay_t<resolver_type<Value>> const& func) {
	if (resolver_name.empty()) {
		throw std::runtime_error("tomlex::register_resolver: empty resolver_type name");
	}
	if (auto it = resolver_table<Value>.find(resolver_name); it != resolver_table<Value>.end()) {
		throw std::runtime_error("tomlex::register_resolver: resolver_type \"" + resolver_name +
								 "\" is already registered");
	}
	resolver_table<Value>[resolver_name] = func;
}

template <typename Value = toml::value>
void clear_resolvers() {
	resolver_table<Value>.clear();
}

template <typename Value = toml::value>
void clear_resolver(std::string const& func_name) {
	if (auto it = resolver_table<Value>.find(func_name); it == resolver_table<Value>.end()) {
		throw std::runtime_error("tomlex::clear_resolver: specified resolver_name \"" + func_name +
								 "\" is not found");
	}
	resolver_table<Value>.erase(func_name);
}

template <typename Value = toml::value>
Value merge(Value&& base, Value&& overwrite) {
	// note: array is overwritten by "overwrite" obj
	if (!base.is_table()) {
		std::ostringstream msg;
		msg << "tomlex::merge: following value must be a table, but " << base.type() << std::endl
			<< base;
		throw std::runtime_error(msg.str());
	}
	if (!overwrite.is_table()) {
		std::ostringstream msg;
		msg << "tomlex::merge: following value must be a table, but " << overwrite.type()
			<< std::endl
			<< overwrite;
		throw std::runtime_error(msg.str());
	}
	auto& base_t = base.as_table();
	for (auto&& [key, value] : overwrite.as_table()) {
		// contains
		if (auto it = base_t.find(key); it != base_t.end()) {
			auto& base_val = it->second;
			if (base_val.type() != value.type()) {
				std::ostringstream msg;
				msg << "tomlex::merge: type mismatch " << base_val.type() << " and " << value.type()
					<< std::endl
					<< base << std::endl
					<< value;
				throw std::runtime_error(msg.str());
			}
			if (value.is_table()) {
				base[key] = merge<Value>(std::move(it->second), std::move(value));
				continue;
			}
		}
		base[key] = std::move(value);
	}
	return base;
}

template <typename Value = toml::value>
Value from_dotted_keys(std::vector<std::string> const& key_list) {
	typename Value::table_type ret;
	for (const auto& key : key_list) {
		toml::detail::location loc(key, key);
		ret = merge(std::move(toml::value(ret)), detail::parse_toml_literal<Value>(loc)).as_table();
	}
	return ret;
}

template <typename Value = toml::value>
Value from_cli(const int argc, char const* const argv[], const int first = 1) {
	if (first >= argc) {
		throw std::runtime_error("tomlex::from_cli: first < argc must be satisfied");
	}
	std::vector<std::string> arg_list(argv + first, argv + argc);
	return from_dotted_keys<Value>(arg_list);
}

template <typename Value = toml::value>
Value resolve(Value&& root_) {
	std::unordered_set<std::string> interpolating_;
	detail::resolve_impl(root_, root_, interpolating_);
	return std::move(root_);
}
template <typename Value = toml::value, typename U>
Value parse(U&& filename) {
	return tomlex::resolve<Value>(toml::parse(std::forward<U>(filename)));
}

namespace detail {
// foward decl
template <typename Value>
Value resolve_each(Value&& val, Value const& root_,
				   std::unordered_set<std::string>& interpolating_);

template <typename Value>
toml::result<Value, std::string> parse_value_strict(toml::detail::location& loc);

template <typename Value>
Value to_toml_value(std::string const& str) {
	if (str.empty()) {
		throw std::runtime_error(
			"tomlex::detail::to_toml_value: cannot convert empty string to toml::value");
	}
	toml::detail::location loc(str, str);
	try {
		const auto result = parse_value_strict<Value>(loc);
		if (result.is_err()) {
			// std::cout << result.as_err() << std::endl;
			throw std::runtime_error(result.as_err());
		}
		return result.unwrap();
	} catch (toml::syntax_error& e) {
		// std::cout << e.what() << std::endl;
		throw std::runtime_error(e.what());
	}
}

template <typename Value>
void resolve_impl(Value& val, Value const& root_, std::unordered_set<std::string>& interpolating_) {
	if (!val.is_table()) {
		return;
	}
	for (auto& [k, v] : val.as_table()) {
		if (v.is_string()) {
			val[k] = std::move(resolve_each(std::move(v), root_, interpolating_));
		} else if (v.is_table()) {
			resolve_impl(v, root_, interpolating_);
		}
	}
}

template <typename Value>
Value interp(std::string_view dst, Value const& root_,
			 std::unordered_set<std::string>& interpolating_) {
	if (dst.empty()) {
		throw std::runtime_error("tomlex::detail::interp: empty interpolation key");
	}

	std::string key(dst);
	if (interpolating_.find(key) != interpolating_.end()) {
		throw std::runtime_error(
			"tomlex::detail::register_resolver: circular reference detected: keyword: \"" + key +
			"\"");
	}
	interpolating_.insert(key);

	// stringで返しているので遅いが、toml11のatはstd::string_viewをとれないのでこれで問題ない
	auto splitted = utils::split(key, '.');

	Value const* node = &root_;
	for (auto& item : splitted) {
		if (!node->contains(item)) {
			throw std::runtime_error("tomlex::detail::register_resolver: interpolation key \"" +
									 item + "\" in \"" + key + "\" is not found");
		}
		Value const& tmp = node->at(item);
		node = &tmp;
	}
	Value ret = *node;	// copy
	Value result = resolve_each(std::move(ret), root_, interpolating_);
	interpolating_.erase(key);
	return result;
}

template <typename Value>
Value apply_custom_resolver(std::string_view resolver_name, std::string_view arr_str,
							Value const& root_, std::unordered_set<std::string>& interpolating_) {
	if (resolver_name.empty()) {
		throw std::runtime_error("tomlex::detail::apply_custom_resolver: empty resolver_name");
	}
	std::string key(resolver_name);
	if (auto it = resolver_table<Value>.find(key); it != resolver_table<Value>.end()) {
		resolver_type<Value> func = it->second;
		Value result;
		if (arr_str.empty()) {
			result = func(Value{});
		} else {
			result = func(to_toml_value<Value>(std::string(arr_str)));
		}
		return resolve_each(std::move(result), root_, interpolating_);
	}  // namespace detail
	std::ostringstream oss;
	oss << "tomlex::detail::apply_custom_resolver: non-registered resolver_type: \"" + key + "\", "
		<< "registered: ";
	for (const auto& [k, v] : resolver_table<Value>) {
		oss << k << ", ";
	}
	throw std::runtime_error(oss.str());
}  // namespace tomlex

template <typename Value>
std::string to_string(Value const& val) {
	std::ostringstream oss;
	if (val.is_string()) {
		return val.as_string();
	}

	if (val.is_array()) {
		oss << '[';
		for (auto const& item : val.as_array()) {
			oss << to_string<Value>(item) << ',';
		}
		oss.seekp(-1, oss.cur);
		oss << ']';
		return oss.str();
	}
	if (val.is_table()) {
		oss << '{';
		for (auto const& [k, v] : val.as_table()) {
			oss << to_string<Value>(k) << "=" << to_string<Value>(v) << ',';
		}
		oss.seekp(-1, oss.cur);
		oss << '}';
		return oss.str();
	}
	oss << val;
	auto ret = oss.str();

	// if (val.is_floating()) {
	//	auto val_float = (val.as_floating());
	//	if (std::isnan(val_float) || std::isinf(val_float)) {
	//		ret.erase(ret.size() - 2, 2);  // remvoe ".0" from "nan.0" and "inf.0"
	//	}
	//}
	return ret;
}

template <typename Value>
Value evaluate(std::string_view expr, Value const& root_,
			   std::unordered_set<std::string>& interpolating_) {
	auto pos_first_colon = expr.find(':');

	// コロンがないのでinterp
	if (pos_first_colon == std::string::npos) {
		expr = utils::trim(expr);
		auto evaluated = interp(expr, root_, interpolating_);
		return evaluated;
	}

	// 関数適用
	std::string_view func_name = utils::trim(expr.substr(0, pos_first_colon));
	std::string_view args = utils::trim(expr.substr(pos_first_colon + 1));
	auto evaluated = apply_custom_resolver(func_name, args, root_, interpolating_);
	return evaluated;
}

int calc_charsize(unsigned char const lead) {
	if (lead < 0x80) {
		return 1;
	}
	if (lead < 0xE0) {
		return 2;
	}
	if (lead < 0xF0) {
		return 3;
	}
	return 4;
}
template <typename Value>
Value resolve_each(Value&& val, Value const& root_,
				   std::unordered_set<std::string>& interpolating_) {
	if (!val.is_string()) {
		return std::move(val);
	}

	bool dollar_found = false;
	int step_size = 0;
	std::string value_str = std::move(val.as_string());
	std::stack<std::pair<size_t, bool>> dist_left_bracket_st;

	for (auto it = value_str.begin(); it != value_str.end(); it += step_size) {
		unsigned char char_ = *it;
		step_size = calc_charsize(char_);

		switch (char_) {
			case '$':
				dollar_found = true;
				break;
			case '{':
				dist_left_bracket_st.push(
					std::make_pair(std::distance(value_str.begin(), it), dollar_found));
				dollar_found = false;
				break;
			case '}': {
				dollar_found = false;
				if (dist_left_bracket_st.empty()) {
					break;
				}
				auto const& [dist_left, enable_eval] = dist_left_bracket_st.top();
				dist_left_bracket_st.pop();
				if (!enable_eval) {
					break;
				}
				auto left = value_str.begin() + dist_left;
				try {
					auto evaluated =
						evaluate(std::string_view{&(*(left + 1)),
												  static_cast<size_t>(std::distance(left + 1, it))},
								 root_, interpolating_);
					// パースする文字列の先頭が"${"で後端が"}"の場合は、toml::valueをそのまま返す
					if ((left - 1) == value_str.begin() && (it + 1) == value_str.end()) {
						return evaluated;
					}
					auto evaluated_str = to_string(evaluated);
					value_str.erase(left - 1, it + 1);
					auto dist_to_dollar = std::distance(value_str.begin(), left - 1);
					value_str.insert(dist_to_dollar, evaluated_str);
					it = value_str.begin() + dist_to_dollar + evaluated_str.size();
					step_size = 0;
				} catch (std::exception& e) {
					std::ostringstream oss;
					auto err = std::string(e.what());
					utils::replace_all(err, "\n", "\n  ");
					oss << "error while processing " << val << std::endl << "  " << err;
					throw std::runtime_error(oss.str());
				}
				break;
			}
			default:
				dollar_found = false;
				break;
		};
	}
	while (!dist_left_bracket_st.empty()) {
		auto const& [dist_left, enable_eval] = dist_left_bracket_st.top();
		dist_left_bracket_st.pop();
		if (!enable_eval) {
			continue;
		}
		std::cerr << "tomlex: warning while parsing " << val << std::endl
				  << "  \"${\" is found, but \"}\" is missing" << std::endl;
	}
	return value_str;
}

template <typename Value = toml::value, typename... Keys>
Value find(Value const& root, Value const& cfg, Keys&&... keys) {
	std::unordered_set<std::string> interpolating;
	Value val = toml::find(cfg, std::forward<Keys>(keys)...);
	return resolve_each(std::move(val), root, interpolating);
}

template <typename Value = toml::value, typename... Keys>
Value find_from_root(Value const& root, Keys&&... keys) {
	std::unordered_set<std::string> interpolating;
	Value val = toml::find(root, std::forward<Keys>(keys)...);
	return resolve_each(std::move(val), root, interpolating);
}

// following code is derived from toml11
/*
The MIT License (MIT)

Copyright (c) 2017 Toru Niina

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
// orig: toml/parser.hpp
inline toml::result<toml::value_t, std::string> guess_number_type_strict(
	const toml::detail::location& l) {
	// suffixが付いていたらパース失敗とする
	using namespace toml;
	using namespace toml::detail;

	location loc = l;

	if (lex_offset_date_time::invoke(loc)) {
		if (loc.iter() != loc.end()) {
			return err(format_underline("bad offset_date_time: invalid suffix",
										{{source_location(loc), "here"}}));
		}
		return ok(value_t::offset_datetime);
	}
	loc.reset(l.iter());

	if (lex_local_date_time::invoke(loc)) {
		if (loc.iter() != loc.end()) {
			return err(format_underline("bad local_date_time: invalid suffix",
										{{source_location(loc), "here"}}));
		}
		return ok(value_t::local_datetime);
	}
	loc.reset(l.iter());

	if (lex_local_date::invoke(loc)) {
		if (loc.iter() != loc.end()) {
			return err(format_underline("bad local_date: invalid suffix",
										{{source_location(loc), "here"}}));
		}
		return ok(value_t::local_date);
	}
	loc.reset(l.iter());

	if (lex_local_time::invoke(loc)) {
		if (loc.iter() != loc.end()) {
			return err(format_underline("bad local_time: invalid suffix",
										{{source_location(loc), "here"}}));
		}
		return ok(value_t::local_time);
	}
	loc.reset(l.iter());

	if (lex_float::invoke(loc)) {
		if (loc.iter() != loc.end()) {
			return err(
				format_underline("bad float: invalid suffix", {{source_location(loc), "here"}}));
		}
		return ok(value_t::floating);
	}
	loc.reset(l.iter());
	if (lex_integer::invoke(loc)) {
		if (loc.iter() != loc.end()) {
			return err(
				format_underline("bad integer: invalid suffix", {{source_location(loc), "here"}}));
		}
		return ok(value_t::integer);
	}
	loc.reset(l.iter());
	if (lex_boolean::invoke(loc)) {
		if (loc.iter() != loc.end()) {
			return err(
				format_underline("bad boolean: invalid suffix", {{source_location(loc), "here"}}));
		}
		return ok(value_t::boolean);
	}
	loc.reset(l.iter());
	if (lex_string::invoke(loc)) {
		if (loc.iter() != loc.end()) {
			return err(
				format_underline("bad string: invalid suffix", {{source_location(loc), "here"}}));
		}
		return ok(value_t::string);
	}
	return err(
		format_underline("bad format: unknown value appeared", {{source_location(loc), "here"}}));
}

inline toml::result<toml::value_t, std::string> guess_value_type_strict(
	const toml::detail::location& loc) {
	using namespace toml;
	using namespace toml::detail;
	switch (*loc.iter()) {
		case '[': {
			if ((*(loc.end() - 1)) == ']') {
				return ok(value_t::array);
			}
			return err(format_underline("bad array", {{source_location(loc), "unknown"}}));
		}
		case '{': {
			if ((*(loc.end() - 1)) == '}') {
				return ok(value_t::table);
			}
			return err(format_underline("bad table", {{source_location(loc), "unknown"}}));
		}
		default: {
			return guess_number_type_strict(loc);
		}
	}
}

template <typename Value>
toml::result<Value, std::string> parse_value_strict(toml::detail::location& loc) {
	using namespace toml;
	using namespace toml::detail;
	const auto first = loc.iter();
	if (first == loc.end()) {
		return err(format_underline("tomlex::detail::parse_value: input is empty",
									{{source_location(loc), ""}}));
	}

	const auto type = guess_value_type_strict(loc);
	if (!type) {
		return err(type.unwrap_err());
	}

	switch (type.unwrap()) {
		case value_t::boolean: {
			return parse_value_helper<Value>(parse_boolean(loc));
		}
		case value_t::integer: {
			return parse_value_helper<Value>(parse_integer(loc));
		}
		case value_t::floating: {
			return parse_value_helper<Value>(parse_floating(loc));
		}
		case value_t::string: {
			return parse_value_helper<Value>(parse_string(loc));
		}
		case value_t::offset_datetime: {
			return parse_value_helper<Value>(parse_offset_datetime(loc));
		}
		case value_t::local_datetime: {
			return parse_value_helper<Value>(parse_local_datetime(loc));
		}
		case value_t::local_date: {
			return parse_value_helper<Value>(parse_local_date(loc));
		}
		case value_t::local_time: {
			return parse_value_helper<Value>(parse_local_time(loc));
		}
		case value_t::array: {
			return parse_value_helper<Value>(parse_array<Value>(loc));
		}
		case value_t::table: {
			return parse_value_helper<Value>(parse_inline_table<Value>(loc));
		}
		default: {
			const auto msg = format_underline(
				"tomlex::detail::parse_value: "
				"unknown token appeared",
				{{source_location(loc), "unknown"}});
			loc.reset(first);
			return err(msg);
		}
	}
}

// from toml::literals::toml_literals
template <typename Value>
Value parse_toml_literal(toml::detail::location loc) {
	// if there are some comments or empty lines, skip them.
	using skip_line = ::toml::detail::repeat<
		toml::detail::sequence<::toml::detail::maybe<::toml::detail::lex_ws>,
							   ::toml::detail::maybe<::toml::detail::lex_comment>,
							   ::toml::detail::lex_newline>,
		::toml::detail::at_least<1>>;
	skip_line::invoke(loc);

	// if there are some whitespaces before a value, skip them.
	using skip_ws = ::toml::detail::repeat<::toml::detail::lex_ws, ::toml::detail::at_least<1>>;
	skip_ws::invoke(loc);

	// to distinguish arrays and tables, first check it is a table or not.
	//
	// "[1,2,3]"_toml;   // this is an array
	// "[table]"_toml;   // a table that has an empty table named "table" inside.
	// "[[1,2,3]]"_toml; // this is an array of arrays
	// "[[table]]"_toml; // this is a table that has an array of tables inside.
	//
	// "[[1]]"_toml;     // this can be both... (currently it becomes a table)
	// "1 = [{}]"_toml;  // this is a table that has an array of table named 1.
	// "[[1,]]"_toml;    // this is an array of arrays.
	// "[[1],]"_toml;    // this also.

	const auto the_front = loc.iter();

	const bool is_table_key = ::toml::detail::lex_std_table::invoke(loc);
	loc.reset(the_front);

	const bool is_aots_key = ::toml::detail::lex_array_table::invoke(loc);
	loc.reset(the_front);

	// If it is neither a table-key or a array-of-table-key, it may be a value.
	if (!is_table_key && !is_aots_key) {
		if (auto data = ::toml::detail::parse_value<Value>(loc)) {
			return data.unwrap();
		}
	}

	// Note that still it can be a table, because the literal might be something
	// like the following.
	// ```cpp
	// R"( // c++11 raw string literals
	//   key = "value"
	//   int = 42
	// )"_toml;
	// ```
	// It is a valid toml file.
	// It should be parsed as if we parse a file with this content.

	if (auto data = ::toml::detail::parse_toml_file<Value>(loc)) {
		return data.unwrap();
	} else	// none of them.
	{
		throw ::toml::syntax_error(data.unwrap_err(), toml::source_location(loc));
	}
}

}  // namespace detail
}  // namespace tomlex
