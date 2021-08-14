#pragma once
#include <toml.hpp>

#include "tomlex.hpp"

namespace tomlex {
namespace resolvers {
toml::value decode(toml::value const& args) {
	switch (args.type()) {
		case toml::value_t::string: {
			return tomlex::detail::to_toml_value(args.as_string());
		}
		default:
			return args;
	}
}

toml::value env(toml::value const& args) {
	switch (args.type()) {
		case toml::value_t::string: {
			std::string target = args.as_string();
#if _WIN32
			char* buf = nullptr;
			size_t size_ = 0;
			if (_dupenv_s(&buf, &size_, target.c_str()) == 0 && buf != nullptr) {
				std::string env_var =
					std::string(buf, size_ - 1);  // size_ includes the null-terminator
				free(buf);
				return env_var;
			}
#else
			if (const char* p = std::getenv(target.c_str())) return std::string(p);
#endif
			throw std::runtime_error("cannot get the environment variable: '" + target + "'");
		}
		default:
			throw std::runtime_error("tomlex::resolver::env accepts only string");
	}
}

}  // namespace resolvers

}  // namespace tomlex