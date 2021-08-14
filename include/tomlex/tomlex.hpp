﻿#pragma once

#include <algorithm>
#include <functional>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>
#include <toml.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tomlex {
using std::string;
using std::string_view;
using std::vector;
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

inline vector<std::string> split(const string& str, const char delim) {
	if (str.empty()) {
		return {""};
	}

	vector<string> res;
	std::istringstream ss(str);
	string buffer;
	while (std::getline(ss, buffer, delim)) {
		res.push_back(buffer);
	}
	if (str[str.length() - 1] == delim) {
		res.push_back("");
	}
	return res;
}

constexpr auto ws = " \t\n\r\f\v";

inline string_view rtrim(string_view s, const char* t = ws) {
	auto pos = s.find_last_not_of(t);
	if (pos == std::string::npos) {
		s.remove_suffix(s.size());
	} else {
		s.remove_suffix(s.size() - pos - 1);
	}
	return s;
}

inline string_view ltrim(string_view s, const char* t = ws) {
	auto pos = std::min(s.find_first_not_of(t), s.size());
	s.remove_prefix(pos);
	return s;
}

inline string_view trim(string_view s, const char* t = ws) { return ltrim(rtrim(s, t), t); }

}  // namespace utils

using resolver = std::function<toml::value(toml::value const&)>;

static inline std::unordered_map<string, resolver> resolvers_;

void register_resolver(string const& func_name, resolver const& func) {
	if (func_name.empty()) {
		throw std::runtime_error("tomlex::register_resolver: empty resolver name");
	}
	if (auto it = resolvers_.find(func_name); it != resolvers_.end()) {
		throw std::runtime_error("tomlex::register_resolver: resolver \"" + func_name +
								 "\" is already registered");
	}
	resolvers_[func_name] = func;
}

void clear_resolvers() { resolvers_.clear(); }

toml::value from_dotted_keys(vector<string> const& key_list) {
	toml::table table;
	for (const auto& key : key_list) {
		toml::detail::location loc(key, key);
		auto val = toml::literals::literal_internal_impl(loc);
		if (!val.is_table()) {
			std::ostringstream oss;
			oss << "cannot recognize as a table: \"" << val << "\"";
			throw std::runtime_error(oss.str());
		}
		table.merge(std::move(val.as_table()));
	}
	return table;
}

toml::value from_cli(const int argc, char* const argv[], int first = 1) {
	if (first >= argc) {
		throw std::runtime_error("tomlex::from_cli: first < argc must be satisfied");
	}
	std::vector<std::string> arg_list(argv + first, argv + argc);
	return from_dotted_keys(arg_list);
}

namespace detail {
void resolve_impl(toml::value& val, toml::value const& root_,
				  std::unordered_set<std::string>& interpolating_);
}  // namespace detail

toml::value resolve(toml::value&& root_) {
	std::unordered_set<string> interpolating_;
	detail::resolve_impl(root_, root_, interpolating_);
	return root_;
}

template <class T>
toml::value parse(T&& filename) {
	return tomlex::resolve(toml::parse(std::forward<T>(filename)));
}

namespace detail {
toml::value resolve_each(toml::value const& val, toml::value const& root_,
						 std::unordered_set<string>& interpolating_);

void resolve_impl(toml::value& val, toml::value const& root_,
				  std::unordered_set<string>& interpolating_) {
	if (!val.is_table()) {
		return;
	}
	for (auto& [k, v] : val.as_table()) {
		if (v.is_string()) {
			val[k] = resolve_each(v, root_, interpolating_);
		} else if (v.is_table()) {
			resolve_impl(v, root_, interpolating_);
		}
	}
}

toml::value interp(string_view dst, toml::value const& root_,
				   std::unordered_set<string>& interpolating_) {
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

	// stringで返しているので遅いが、toml11のatはstring_viewをとれないのでこれで問題ない
	auto splitted = utils::split(key, '.');

	toml::value const* node = &root_;
	for (auto& item : splitted) {
		if (!node->contains(item)) {
			throw std::runtime_error("tomlex::detail::register_resolver: interpolation key \"" +
									 item + "\" in \"" + key + "\" is not found");
		}
		toml::value const& tmp = node->at(item);
		node = &tmp;
	}
	toml::value ret = resolve_each(*node, root_, interpolating_);
	interpolating_.erase(key);
	return ret;
}

toml::value apply_custom_resolver(string_view func_name, toml::value const& arr,
								  toml::value const& root_,
								  std::unordered_set<string>& interpolating_) {
	if (func_name.empty()) {
		throw std::runtime_error("tomlex::detail::apply_custom_resolver: empty func_name");
	}
	std::string key(func_name);
	if (auto it = resolvers_.find(key); it != resolvers_.end()) {
		auto func = it->second;
		return resolve_each(func(arr), root_, interpolating_);
	}
	std::ostringstream oss;
	oss << "tomlex::detail::apply_custom_resolver: non-registered resolver: \"" + key + "\", "
		<< "registered: ";
	for (const auto& [k, v] : resolvers_) {
		oss << k << ", ";
	}
	throw std::runtime_error(oss.str());
}

template <typename Value>
toml::result<Value, std::string> parse_value_strict(toml::detail::location& loc);

toml::value to_toml_value(string const& str) {
	if (str.empty()) {
		throw std::runtime_error(
			"tomlex::detail::to_toml_value: cannot convert empty string to toml::value");
	}
	toml::detail::location loc(str, str);
	try {
		auto const result = parse_value_strict<toml::value>(loc);
		if (result.is_err()) {
			// std::cout << result.as_err() << std::endl;
			throw std::runtime_error(result.as_err());
		}
		return result.unwrap();
	} catch (toml::syntax_error e) {
		// std::cout << e.what() << std::endl;
		throw std::runtime_error(e.what());
	}
}

string to_string(toml::value const& val) {
	std::ostringstream oss;
	if (val.is_string()) {
		return val.as_string();
	}

	if (val.is_array()) {
		oss << '[';
		for (auto const& item : val.as_array()) {
			oss << to_string(item) << ',';
		}
		oss.seekp(-1, oss.cur);
		oss << ']';
		return oss.str();
	}
	if (val.is_table()) {
		oss << '{';
		for (auto const& [k, v] : val.as_table()) {
			oss << to_string(k) << "=" << to_string(v) << ',';
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

toml::value evaluate(string_view expr, toml::value const& root_,
					 std::unordered_set<string>& interpolating_) {
	auto pos_first_colon = expr.find(':');

	// コロンがないのでinterp
	if (pos_first_colon == string::npos) {
		expr = utils::trim(expr);
		toml::value evaluated = interp(expr, root_, interpolating_);
		return evaluated;
	}

	// 関数適用
	string_view func_name = utils::trim(expr.substr(0, pos_first_colon));
	string_view args = utils::trim(expr.substr(pos_first_colon + 1));
	auto evaluated =
		apply_custom_resolver(func_name, to_toml_value(std::string(args)), root_, interpolating_);
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

toml::value resolve_each(toml::value const& val, toml::value const& root_,
						 std::unordered_set<string>& interpolating_) {
	if (!val.is_string()) {
		return val;
	}

	bool dollar_found = false;
	bool resolved = false;
	int step_size = 0;
	string value_str = val.as_string();
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
				resolved = true;
				auto left = value_str.begin() + dist_left;
				try {
					auto evaluated =
						evaluate(string_view{&(*(left + 1)),
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
	if (resolved) {
		return toml::value(value_str);
	}
	return val;
}

template <class... Tails>
toml::value find(toml::value const& root, toml::value const& cfg, Tails&&... keys) {
	std::unordered_set<string> interpolating;
	return resolve_each(toml::find(cfg, std::forward<Tails>(keys)...), root, interpolating);
}

template <class... Tails>
toml::value find_from_root(toml::value const& root, Tails&&... keys) {
	std::unordered_set<string> interpolating;
	return resolve_each(toml::find(root, std::forward<Tails>(keys)...), root, interpolating);
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

}  // namespace detail
}  // namespace tomlex