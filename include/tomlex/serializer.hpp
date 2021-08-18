#pragma once
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

#include <toml.hpp>

namespace tomlex {
namespace detail {
namespace serializer {
using namespace toml;

template <typename Value>
struct serializer_short {
	static_assert(toml::detail::is_basic_value<Value>::value,
				  "serializer_short is for toml::value and its variants, "
				  "toml::basic_value<...>.");

	using value_type = Value;
	using key_type = typename value_type::key_type;
	using comment_type = typename value_type::comment_type;
	using boolean_type = typename value_type::boolean_type;
	using integer_type = typename value_type::integer_type;
	using floating_type = typename value_type::floating_type;
	using string_type = typename value_type::string_type;
	using local_time_type = typename value_type::local_time_type;
	using local_date_type = typename value_type::local_date_type;
	using local_datetime_type = typename value_type::local_datetime_type;
	using offset_datetime_type = typename value_type::offset_datetime_type;
	using array_type = typename value_type::array_type;
	using table_type = typename value_type::table_type;

	serializer_short(const std::size_t w = 80u,
					 const int float_prec = std::numeric_limits<toml::floating>::max_digits10,
					 const bool can_be_inlined = false, const bool no_comment = false,
					 std::vector<toml::key> ks = {}, const bool value_has_comment = false)
		: can_be_inlined_(can_be_inlined),
		  no_comment_(no_comment),
		  value_has_comment_(value_has_comment && !no_comment),
		  float_prec_(float_prec),
		  width_(w),
		  keys_(std::move(ks)) {}
	~serializer_short() = default;

	std::string operator()(const boolean_type& b) const { return b ? "true" : "false"; }
	std::string operator()(const integer_type i) const { return std::to_string(i); }
	std::string operator()(const floating_type f) const {
		if (std::isnan(f)) {
			if (std::signbit(f)) {
				return std::string("-nan");
			} else {
				return std::string("nan");
			}
		} else if (!std::isfinite(f)) {
			if (std::signbit(f)) {
				return std::string("-inf");
			} else {
				return std::string("inf");
			}
		}

		const auto fmt = "%.*g";
		const auto bsz = std::snprintf(nullptr, 0, fmt, this->float_prec_, f);
		// +1 for null character(\0)
		std::vector<char> buf(static_cast<std::size_t>(bsz + 1), '\0');
		std::snprintf(buf.data(), buf.size(), fmt, this->float_prec_, f);

		std::string token(buf.begin(), std::prev(buf.end()));
		if (!token.empty() && token.back() == '.')	// 1. => 1.0
		{
			token += '0';
		}

		const auto e =
			std::find_if(token.cbegin(), token.cend(),
						 [](const char c) noexcept -> bool { return c == 'e' || c == 'E'; });
		const auto has_exponent = (token.cend() != e);
		const auto has_fraction = (token.cend() != std::find(token.cbegin(), token.cend(), '.'));

		if (!has_exponent && !has_fraction) {
			// the resulting value does not have any float specific part!
			token += ".0";
		}
		return token;
	}
	std::string operator()(const string_type& s) const {
		if (s.kind == string_t::basic) {
			if ((std::find(s.str.cbegin(), s.str.cend(), '\n') != s.str.cend() ||
				 std::find(s.str.cbegin(), s.str.cend(), '\"') != s.str.cend()) &&
				this->width_ != (std::numeric_limits<std::size_t>::max)()) {
				// if linefeed or double-quote is contained,
				// make it multiline basic string.
				const auto escaped = this->escape_ml_basic_string(s.str);
				std::string open("\"\"\"");
				std::string close("\"\"\"");
				if (escaped.find('\n') != std::string::npos || this->width_ < escaped.size() + 6) {
					// if the string body contains newline or is enough long,
					// add newlines after and before delimiters.
					open += "\n";
					close = std::string("\\\n") + close;
				}
				return open + escaped + close;
			}

			// no linefeed. try to make it oneline-string.
			std::string oneline = this->escape_basic_string(s.str);
			if (oneline.size() + 2 < width_ || width_ < 2) {
				const std::string quote("\"");
				return quote + oneline + quote;
			}

			// the line is too long compared to the specified width.
			// split it into multiple lines.
			std::string token("\"\"\"\n");
			while (!oneline.empty()) {
				if (oneline.size() < width_) {
					token += oneline;
					oneline.clear();
				} else if (oneline.at(width_ - 2) == '\\') {
					token += oneline.substr(0, width_ - 2);
					token += "\\\n";
					oneline.erase(0, width_ - 2);
				} else {
					token += oneline.substr(0, width_ - 1);
					token += "\\\n";
					oneline.erase(0, width_ - 1);
				}
			}
			return token + std::string("\\\n\"\"\"");
		} else	// the string `s` is literal-string.
		{
			if (std::find(s.str.cbegin(), s.str.cend(), '\n') != s.str.cend() ||
				std::find(s.str.cbegin(), s.str.cend(), '\'') != s.str.cend()) {
				std::string open("'''");
				if (this->width_ + 6 < s.str.size()) {
					open += '\n';  // the first newline is ignored by TOML spec
				}
				const std::string close("'''");
				return open + s.str + close;
			} else {
				const std::string quote("'");
				return quote + s.str + quote;
			}
		}
	}

	std::string operator()(const local_date_type& d) const {
		std::ostringstream oss;
		oss << d;
		return oss.str();
	}
	std::string operator()(const local_time_type& t) const {
		std::ostringstream oss;
		oss << t;
		return oss.str();
	}
	std::string operator()(const local_datetime_type& dt) const {
		std::ostringstream oss;
		oss << dt;
		return oss.str();
	}
	std::string operator()(const offset_datetime_type& odt) const {
		std::ostringstream oss;
		oss << odt;
		return oss.str();
	}

	std::string operator()(const array_type& v) const {
		if (v.empty()) {
			return std::string("[]");
		}
		if (this->is_array_of_tables(v)) {
			return make_array_of_tables(v);
		}

		// not an array of tables. normal array.
		// first, try to make it inline if none of the elements have a comment.
		if (!this->has_comment_inside(v)) {
			const auto inl = this->make_inline_array(v);
			if (inl.size() < this->width_ &&
				std::find(inl.cbegin(), inl.cend(), '\n') == inl.cend()) {
				return inl;
			}
		}

		// if the length exceeds this->width_, print multiline array.
		// key = [
		//   # ...
		//   42,
		//   ...
		// ]
		std::string token;
		std::string current_line;
		token += "[\n";
		for (const auto& item : v) {
			if (!item.comments().empty() && !no_comment_) {
				// if comment exists, the element must be the only element in the line.
				// e.g. the following is not allowed.
				// ```toml
				// array = [
				// # comment for what?
				// 1, 2, 3, 4, 5
				// ]
				// ```
				if (!current_line.empty()) {
					if (current_line.back() != '\n') {
						current_line += '\n';
					}
					token += current_line;
					current_line.clear();
				}
				for (const auto& c : item.comments()) {
					token += '#';
					token += c;
					token += '\n';
				}
				token += toml::visit(*this, item);
				if (!token.empty() && token.back() == '\n') {
					token.pop_back();
				}
				token += ",\n";
				continue;
			}
			std::string next_elem;
			if (item.is_table()) {
				serializer_short ser(*this);
				ser.can_be_inlined_ = true;
				ser.width_ = (std::numeric_limits<std::size_t>::max)();
				next_elem += toml::visit(ser, item);
			} else {
				next_elem += toml::visit(*this, item);
			}

			// comma before newline.
			if (!next_elem.empty() && next_elem.back() == '\n') {
				next_elem.pop_back();
			}

			// if current line does not exceeds the width limit, continue.
			if (current_line.size() + next_elem.size() + 1 < this->width_) {
				current_line += next_elem;
				current_line += ',';
			} else if (current_line.empty()) {
				// if current line was empty, force put the next_elem because
				// next_elem is not splittable
				token += next_elem;
				token += ",\n";
				// current_line is kept empty
			} else	// reset current_line
			{
				assert(current_line.back() == ',');
				token += current_line;
				token += '\n';
				current_line = next_elem;
				current_line += ',';
			}
		}
		if (!current_line.empty()) {
			if (!current_line.empty() && current_line.back() != '\n') {
				current_line += '\n';
			}
			token += current_line;
		}
		token += "]\n";
		return token;
	}

	// templatize for any table-like container
	std::string operator()(const table_type& v) const {
		// if an element has a comment, then it can't be inlined.
		// table = {# how can we write a comment for this? key = "value"}
		/*
		if (this->can_be_inlined_ && !(this->has_comment_inside(v))) {
			std::string token;
			if (!this->keys_.empty()) {
				token += format_key(this->keys_.back());
				token += " = ";
			}
			token += this->make_inline_table(v);
			//テーブルはインライン化しない
			//if (token.size() < this->width_ &&
			//	token.end() == std::find(token.begin(), token.end(), '\n')) {
			//	return token;
			//}
		}
		*/

		std::string token;
		auto [token_tmp, cnt_non_table] = this->make_multiline_table(v);
		if ((token_tmp == "" || cnt_non_table > 0) && !keys_.empty()) {
			token += '[';
			token += format_keys(keys_);
			token += "]\n";
		}
		token += token_tmp;
		return token;
	}

   private:
	std::string escape_basic_string(const std::string& s) const {
		// XXX assuming `s` is a valid utf-8 sequence.
		std::string retval;
		for (const char c : s) {
			switch (c) {
				case '\\': {
					retval += "\\\\";
					break;
				}
				case '\"': {
					retval += "\\\"";
					break;
				}
				case '\b': {
					retval += "\\b";
					break;
				}
				case '\t': {
					retval += "\\t";
					break;
				}
				case '\f': {
					retval += "\\f";
					break;
				}
				case '\n': {
					retval += "\\n";
					break;
				}
				case '\r': {
					retval += "\\r";
					break;
				}
				default: {
					if ((0x00 <= c && c <= 0x08) || (0x0A <= c && c <= 0x1F) || c == 0x7F) {
						retval += "\\u00";
						retval += char(48 + (c / 16));
						retval += char((c % 16 < 10 ? 48 : 55) + (c % 16));
					} else {
						retval += c;
					}
				}
			}
		}
		return retval;
	}

	std::string escape_ml_basic_string(const std::string& s) const {
		std::string retval;
		for (auto i = s.cbegin(), e = s.cend(); i != e; ++i) {
			switch (*i) {
				case '\\': {
					retval += "\\\\";
					break;
				}
					// One or two consecutive "s are allowed.
					// Later we will check there are no three consecutive "s.
					//   case '\"': {retval += "\\\""; break;}
				case '\b': {
					retval += "\\b";
					break;
				}
				case '\t': {
					retval += "\\t";
					break;
				}
				case '\f': {
					retval += "\\f";
					break;
				}
				case '\n': {
					retval += "\n";
					break;
				}
				case '\r': {
					if (std::next(i) != e && *std::next(i) == '\n') {
						retval += "\r\n";
						++i;
					} else {
						retval += "\\r";
					}
					break;
				}
				default: {
					const auto c = *i;
					if ((0x00 <= c && c <= 0x08) || (0x0A <= c && c <= 0x1F) || c == 0x7F) {
						retval += "\\u00";
						retval += char(48 + (c / 16));
						retval += char((c % 16 < 10 ? 48 : 55) + (c % 16));
					} else {
						retval += c;
					}
				}
			}
		}
		// Only 1 or 2 consecutive `"`s are allowed in multiline basic string.
		// 3 consecutive `"`s are considered as a closing delimiter.
		// We need to check if there are 3 or more consecutive `"`s and insert
		// backslash to break them down into several short `"`s like the `str6`
		// in the following example.
		// ```toml
		// str4 = """Here are two quotation marks: "". Simple enough."""
		// # str5 = """Here are three quotation marks: """."""  # INVALID
		// str5 = """Here are three quotation marks: ""\"."""
		// str6 = """Here are fifteen quotation marks: ""\"""\"""\"""\"""\"."""
		// ```
		auto found_3_quotes = retval.find("\"\"\"");
		while (found_3_quotes != std::string::npos) {
			retval.replace(found_3_quotes, 3, "\"\"\\\"");
			found_3_quotes = retval.find("\"\"\"");
		}
		return retval;
	}

	// if an element of a table or an array has a comment, it cannot be inlined.
	bool has_comment_inside(const array_type& a) const noexcept {
		// if no_comment is set, comments would not be written.
		if (this->no_comment_) {
			return false;
		}

		for (const auto& v : a) {
			if (!v.comments().empty()) {
				return true;
			}
		}
		return false;
	}
	bool has_comment_inside(const table_type& t) const noexcept {
		// if no_comment is set, comments would not be written.
		if (this->no_comment_) {
			return false;
		}

		for (const auto& kv : t) {
			if (!kv.second.comments().empty()) {
				return true;
			}
		}
		return false;
	}

	std::string make_inline_array(const array_type& v) const {
		assert(!has_comment_inside(v));
		std::string token;
		token += '[';
		bool is_first = true;
		for (const auto& item : v) {
			if (is_first) {
				is_first = false;
			} else {
				token += ',';
			}
			token +=
				visit(serializer_short((std::numeric_limits<std::size_t>::max)(), this->float_prec_,
									   /* inlined */ true, /*no comment*/ false, /*keys*/ {},
									   /*has_comment*/ !item.comments().empty()),
					  item);
		}
		token += ']';
		return token;
	}

	std::string make_inline_table(const table_type& v) const {
		assert(!has_comment_inside(v));
		assert(this->can_be_inlined_);
		std::string token;
		token += '{';
		bool is_first = true;
		for (const auto& kv : v) {
			// in inline tables, trailing comma is not allowed (toml-lang #569).
			if (is_first) {
				is_first = false;
			} else {
				token += ',';
			}
			token += format_key(kv.first);
			token += '=';
			token +=
				visit(serializer_short((std::numeric_limits<std::size_t>::max)(), this->float_prec_,
									   /* inlined */ true, /*no comment*/ false, /*keys*/ {},
									   /*has_comment*/ !kv.second.comments().empty()),
					  kv.second);
		}
		token += '}';
		return token;
	}

	std::pair<std::string, size_t> make_multiline_table(const table_type& v) const {
		std::string token;
		size_t elem_non_table = 0;

		// print non-table elements first.
		// ```toml
		// [foo]         # a table we're writing now here
		// key = "value" # <- non-table element, "key"
		// # ...
		// [foo.bar] # <- table element, "bar"
		// ```
		// because after printing [foo.bar], the remaining non-table values will
		// be assigned into [foo.bar], not [foo]. Those values should be printed
		// earlier.
		for (const auto& kv : v) {
			if (kv.second.is_table() || is_array_of_tables(kv.second)) {
				continue;
			}

			token += write_comments(kv.second);

			const auto key_and_sep = format_key(kv.first) + " = ";
			const auto residual_width =
				(this->width_ > key_and_sep.size()) ? this->width_ - key_and_sep.size() : 0;
			token += key_and_sep;
			token +=
				visit(serializer_short(residual_width, this->float_prec_,
									   /*can be inlined*/ true, /*no comment*/ false, /*keys*/ {},
									   /*has_comment*/ !kv.second.comments().empty()),
					  kv.second);

			if (token.back() != '\n') {
				token += '\n';
			}
			elem_non_table++;
		}

		// normal tables / array of tables

		// after multiline table appeared, the other tables cannot be inline
		// because the table would be assigned into the table.
		// [foo]
		// ...
		// bar = {...} # <- bar will be a member of [foo].
		bool multiline_table_printed = false;
		for (const auto& kv : v) {
			if (!kv.second.is_table() && !is_array_of_tables(kv.second)) {
				continue;  // other stuff are already serialized. skip them.
			}

			std::vector<toml::key> ks(this->keys_);
			ks.push_back(kv.first);

			auto tmp = visit(serializer_short(this->width_, this->float_prec_,
											  !multiline_table_printed, this->no_comment_, ks,
											  /*has_comment*/ !kv.second.comments().empty()),
							 kv.second);

			// If it is the first time to print a multi-line table, it would be
			// helpful to separate normal key-value pair and subtables by a
			// newline.
			// (this checks if the current key-value pair contains newlines.
			//  but it is not perfect because multi-line string can also contain
			//  a newline. in such a case, an empty line will be written) TODO
			if ((!multiline_table_printed) &&
				std::find(tmp.cbegin(), tmp.cend(), '\n') != tmp.cend()) {
				multiline_table_printed = true;

				if (token != "") {
					token += '\n';	// separate key-value pairs and subtables
				}
				token += write_comments(kv.second);
				token += tmp;

				// care about recursive tables (all tables in each level prints
				// newline and there will be a full of newlines)
				if (tmp.substr(tmp.size() - 2, 2) != "\n\n" &&
					tmp.substr(tmp.size() - 4, 4) != "\r\n\r\n") {
					token += '\n';
				}
			} else {
				token += write_comments(kv.second);
				token += tmp;
				if (tmp.substr(tmp.size() - 2, 2) != "\n\n" &&
					tmp.substr(tmp.size() - 4, 4) != "\r\n\r\n") {
					token += '\n';
				}
			}
		}
		return {token, elem_non_table};
	}

	std::string make_array_of_tables(const array_type& v) const {
		// if it's not inlined, we need to add `[[table.key]]`.
		// but if it can be inlined, we can format it as the following.
		// ```
		// table.key = [
		//   {...},
		//   # comment
		//   {...},
		// ]
		// ```
		// This function checks if inlinization is possible or not, and then
		// format the array-of-tables in a proper way.
		//
		// Note about comments:
		//
		// If the array itself has a comment (value_has_comment_ == true), we
		// should try to make it inline.
		// ```toml
		// # comment about array
		// array = [
		//   # comment about table element
		//   {of = "table"}
		// ]
		// ```
		// If it is formatted as a multiline table, the two comments becomes
		// indistinguishable.
		// ```toml
		// # comment about array
		// # comment about table element
		// [[array]]
		// of = "table"
		// ```
		// So we need to try to make it inline, and it force-inlines regardless
		// of the line width limit.
		//     It may fail if the element of a table has comment. In that case,
		// the array-of-tables will be formatted as a multiline table.
		if (this->can_be_inlined_ || this->value_has_comment_) {
			std::string token;
			if (!keys_.empty()) {
				token += format_key(keys_.back());
				token += " = ";
			}

			bool failed = false;
			token += "[\n";
			for (const auto& item : v) {
				// if an element of the table has a comment, the table
				// cannot be inlined.
				if (this->has_comment_inside(item.as_table())) {
					failed = true;
					break;
				}
				// write comments for the table itself
				token += write_comments(item);

				const auto t = this->make_inline_table(item.as_table());

				if (t.size() + 1 > width_ ||  // +1 for the last comma {...},
					std::find(t.cbegin(), t.cend(), '\n') != t.cend()) {
					// if the value itself has a comment, ignore the line width limit
					if (!this->value_has_comment_) {
						failed = true;
						break;
					}
				}
				token += t;
				token += ",\n";
			}

			if (!failed) {
				token += "]\n";
				return token;
			}
			// if failed, serialize them as [[array.of.tables]].
		}

		std::string token;
		for (const auto& item : v) {
			token += write_comments(item);
			token += "[[";
			token += format_keys(keys_);
			token += "]]\n";
			auto token_tmp = this->make_multiline_table(item.as_table());
			token += token_tmp.first;
		}
		return token;
	}

	std::string write_comments(const value_type& v) const {
		std::string retval;
		if (this->no_comment_) {
			return retval;
		}

		for (const auto& c : v.comments()) {
			retval += '#';
			retval += c;
			retval += '\n';
		}
		return retval;
	}

	bool is_array_of_tables(const value_type& v) const {
		if (!v.is_array() || v.as_array().empty()) {
			return false;
		}
		return is_array_of_tables(v.as_array());
	}
	bool is_array_of_tables(const array_type& v) const {
		// Since TOML v0.5.0, heterogeneous arrays are allowed. So we need to
		// check all the element in an array to check if the array is an array
		// of tables.
		return std::all_of(v.begin(), v.end(),
						   [](const value_type& elem) { return elem.is_table(); });
	}

   private:
	bool can_be_inlined_;
	bool no_comment_;
	bool value_has_comment_;
	int float_prec_;
	std::size_t width_;
	std::vector<toml::key> keys_;
};
}  // namespace serializer
}  // namespace detail
}  // namespace tomlex
